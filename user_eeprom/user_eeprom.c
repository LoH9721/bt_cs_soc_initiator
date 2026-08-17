/***************************************************************************//**
 * @file user_eeprom.c
 * @brief 统一 EEPROM 存储实现: RAM 缓存 + 异步写队列 (深度 16)
 *
 * ── 数据流 ──
 *   init():  for each item → driver->read() → CRC校验 → 填充 RAM 影子表
 *
 *   read(item):
 *     ┌─ g_cache[item].valid ?
 *     │    YES → memcpy(buf, g_cache[item].data, len) → OK
 *     │    NO  → NOT_FOUND
 *     └─ 绝不调用 driver->read()
 *
 *   write(item, data, callback):
 *     1. 立即更新 g_cache[item].data + 标记 dirty
 *     2. 深拷贝 data → 构造 write_task_t → 入环形队列
 *     3. 队列满 → 降级为同步写入 (兜底)
 *     4. 返回 OK (调用者不等待)
 *
 *   process(max_ms):
 *     if 队列空 → return false
 *     task = 出队
 *     driver->write(task.data + CRC16)
 *     成功 → 清除 dirty + 调用 callback(OK)
 *     失败 → 保留 dirty (下次 write 触发重试)
 *     return 队列是否还有剩余
 *
 * ── 断电保护 ──
 *   - write() 立即更新 RAM, read() 返回新值 → 上层逻辑正确
 *   - dirty=true 但 NVM 未更新: 重启后 init() 读取旧 CRC → valid=false → 首次 write 时会重写
 *   - flush() → 关机前确保全队列落盘
 ******************************************************************************/

#include "user_eeprom.h"
#include "user_security.h"
#include <string.h>

/* =================================================================== */
/* 条目描述表 (X-Macro 展开, 编译期常量, 放在 Flash)                    */
/* =================================================================== */

typedef struct {
  uint32_t nvm_key;
  uint16_t data_len;
  uint16_t total_len;    /* data_len + 2 (CRC16) */
  uint8_t  flags;
} eeprom_desc_t;

#define EEPROM_ITEM_X(name, addr, dlen, label, flags) \
  [EEPROM_##name] = { (addr), (dlen), (uint16_t)((dlen) + 2U), (flags) },

static const eeprom_desc_t g_desc[EEPROM_ITEM_COUNT] = {
  #include "user_eeprom_items.def"
};
#undef EEPROM_ITEM_X

/* 最大条目总长度: 67 Byte (公钥 65B + CRC 2B) */
#define EEPROM_MAX_TOTAL_LEN   67U

/* =================================================================== */
/* RAM 影子表                                                           */
/* =================================================================== */

typedef struct {
  uint8_t  data[EEPROM_MAX_TOTAL_LEN];  /* 仅数据部分, 不含 CRC */
  bool     valid : 1;   /* init() 时 CRC 校验通过 */
  bool     dirty : 1;   /* RAM 已更新, NVM 尚未落盘 */
} eeprom_cache_t;

static eeprom_cache_t g_cache[EEPROM_ITEM_COUNT];

/* =================================================================== */
/* 异步写队列 (环形缓冲, 深度 16)                                       */
/* =================================================================== */

#define WRITE_QUEUE_SIZE  16U

typedef struct {
  user_eeprom_item_t item;
  uint8_t  data[EEPROM_MAX_TOTAL_LEN];  /* 深拷贝的原始数据 */
  uint16_t data_len;
  void   (*callback)(user_eeprom_item_t item, sl_status_t result);
  bool     pending;
} write_task_t;

static write_task_t g_write_queue[WRITE_QUEUE_SIZE];
static uint8_t      g_queue_head;   /* 出队位置 */
static uint8_t      g_queue_tail;   /* 入队位置 */
static uint8_t      g_queue_count;

/* =================================================================== */
/* 驱动                                                                 */
/* =================================================================== */

static const user_eeprom_driver_t *g_driver;

void user_eeprom_driver_register(const user_eeprom_driver_t *driver)
{
  g_driver = driver;
}

/* =================================================================== */
/* CRC16 工具 (复用 user_security)                                      */
/* =================================================================== */

static uint16_t eeprom_crc16(const uint8_t *data, uint16_t len)
{
  return user_security_crc16(data, (size_t)len);
}

static void write_u16_be(uint8_t *b, uint16_t v)
{
  b[0] = (uint8_t)(v >> 8);
  b[1] = (uint8_t)(v & 0xFFU);
}

static uint16_t read_u16_be(const uint8_t *b)
{
  return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

/* =================================================================== */
/* 队列操作                                                             */
/* =================================================================== */

static bool queue_is_empty(void) { return g_queue_count == 0U; }
static bool queue_is_full(void)  { return g_queue_count >= WRITE_QUEUE_SIZE; }

static bool queue_push(user_eeprom_item_t item, const void *data, uint16_t len,
                       void (*cb)(user_eeprom_item_t, sl_status_t))
{
  if (queue_is_full()) return false;

  write_task_t *t = &g_write_queue[g_queue_head];
  t->item     = item;
  t->data_len = len;
  memcpy(t->data, data, len);
  t->callback = cb;
  t->pending  = true;

  g_queue_head = (g_queue_head + 1U) % WRITE_QUEUE_SIZE;
  g_queue_count++;
  return true;
}

static bool queue_pop(write_task_t *out)
{
  if (queue_is_empty()) return false;

  *out = g_write_queue[g_queue_tail];
  g_write_queue[g_queue_tail].pending = false;

  g_queue_tail = (g_queue_tail + 1U) % WRITE_QUEUE_SIZE;
  g_queue_count--;
  return true;
}

/* =================================================================== */
/* 内部: 从存储读取并校验 CRC                                           */
/* =================================================================== */

/**
 * @brief 读取 raw 数据 + 校验 CRC
 * @return SL_STATUS_OK 数据有效
 */
static sl_status_t load_and_validate(uint16_t idx)
{
  const eeprom_desc_t *d = &g_desc[idx];
  uint8_t raw[EEPROM_MAX_TOTAL_LEN];
  uint16_t crc_calc, crc_stored;
  sl_status_t sc;

  sc = g_driver->read(d->nvm_key, raw, d->total_len);
  if (sc != SL_STATUS_OK) {
    return sc;
  }

  crc_calc   = eeprom_crc16(raw, d->data_len);
  crc_stored = read_u16_be(&raw[d->data_len]);

  if (crc_calc != crc_stored) {
    return SL_STATUS_NOT_FOUND;  /* CRC 不匹配 → 视为无效 */
  }

  /* 校验通过 → 缓存到 RAM */
  memcpy(g_cache[idx].data, raw, d->data_len);
  g_cache[idx].valid = true;
  g_cache[idx].dirty = false;

  return SL_STATUS_OK;
}

/* =================================================================== */
/* init(): 预加载全部条目                                                */
/* =================================================================== */

void user_eeprom_init(void)
{
  /* 清零缓存 */
  memset(g_cache, 0, sizeof(g_cache));
  g_queue_head  = 0U;
  g_queue_tail  = 0U;
  g_queue_count = 0U;
  memset(g_write_queue, 0, sizeof(g_write_queue));

  if (g_driver == NULL) return;

  /* 驱动初始化 */
  if (g_driver->init != NULL) {
    (void)g_driver->init();
  }

  /* 遍历全部条目 */
  for (uint16_t i = 0U; i < (uint16_t)EEPROM_ITEM_COUNT; i++) {
    const eeprom_desc_t *d = &g_desc[i];

    /* EEPROM_FLAG_AUTO_CLEAR: 上电自动擦除并标记无效 */
    if ((d->flags & EEPROM_FLAG_AUTO_CLEAR) != 0U) {
      (void)g_driver->erase(d->nvm_key);
      g_cache[i].valid = false;
      g_cache[i].dirty = false;
      continue;
    }

    /* 加载并校验 CRC */
    (void)load_and_validate(i);
    /* 校验失败 → g_cache[i].valid = false (memset 已置零) */
  }
}

/* =================================================================== */
/* read(): 零延迟, 直接返回 RAM 缓存                                     */
/* =================================================================== */

sl_status_t user_eeprom_read(user_eeprom_item_t item, void *buf, uint16_t buf_len)
{
  if (item >= EEPROM_ITEM_COUNT || buf == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  const eeprom_desc_t *d = &g_desc[item];
  eeprom_cache_t      *c = &g_cache[item];

  if (!c->valid || buf_len < d->data_len) {
    return SL_STATUS_NOT_FOUND;
  }

  memcpy(buf, c->data, d->data_len);
  return SL_STATUS_OK;
}

/* =================================================================== */
/* write(): 更新 RAM + 异步入队                                          */
/* =================================================================== */

sl_status_t user_eeprom_write(user_eeprom_item_t item,
                              const void *data, uint16_t data_len,
                              void (*callback)(user_eeprom_item_t, sl_status_t))
{
  if (item >= EEPROM_ITEM_COUNT || data == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  const eeprom_desc_t *d = &g_desc[item];
  eeprom_cache_t      *c = &g_cache[item];

  if (data_len != d->data_len) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* 1. 立即更新 RAM 缓存 (后续 read() 立即返回新值) */
  memcpy(c->data, data, d->data_len);
  c->valid = true;
  c->dirty = true;

  /* 2. 入队异步写 */
  if (!queue_push(item, data, data_len, callback)) {
    /* 队列满 → 降级同步写入 (极端场景兜底, 不应频繁发生) */
    return user_eeprom_write_sync(item, data, data_len);
  }

  return SL_STATUS_OK;
}

/* =================================================================== */
/* write_sync(): 阻塞写入 (关机 / 紧急场景)                              */
/* =================================================================== */

sl_status_t user_eeprom_write_sync(user_eeprom_item_t item,
                                   const void *data, uint16_t data_len)
{
  if (item >= EEPROM_ITEM_COUNT || data == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  const eeprom_desc_t *d = &g_desc[item];
  uint8_t raw[EEPROM_MAX_TOTAL_LEN];
  uint16_t crc;
  sl_status_t sc;

  /* data_len 校验 */
  if (data_len != d->data_len) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* 组装: data + CRC16 (BE) */
  memcpy(raw, data, d->data_len);
  crc = eeprom_crc16((const uint8_t *)data, d->data_len);
  write_u16_be(&raw[d->data_len], crc);

  /* 阻塞写入底层存储 */
  sc = g_driver->write(d->nvm_key, raw, d->total_len);
  if (sc == SL_STATUS_OK) {
    /* 同步写入成功 → 更新 RAM 缓存, 后续 read() 才能读到 */
    memcpy(g_cache[item].data, data, d->data_len);
    g_cache[item].valid = true;
    g_cache[item].dirty = false;
  }
  return sc;
}

/* =================================================================== */
/* process(): 从队列取一个任务执行                                       */
/* =================================================================== */

bool user_eeprom_process(uint16_t max_time_ms)
{
  write_task_t task;
  const eeprom_desc_t *d;
  uint8_t raw[EEPROM_MAX_TOTAL_LEN];
  uint16_t crc;
  sl_status_t sc;

  (void)max_time_ms;  /* 当前: 每次调用执行一个任务, 驱动保证 ~5ms 内完成 */

  if (!queue_pop(&task)) {
    return false;  /* 队列空 */
  }

  /* 仅在 dirty 时才真正写入 (可能被 write_sync 抢先落盘) */
  if (!g_cache[task.item].dirty) {
    return !queue_is_empty();
  }

  d = &g_desc[task.item];

  /* 组装 raw + CRC */
  memcpy(raw, task.data, d->data_len);
  crc = eeprom_crc16(task.data, d->data_len);
  write_u16_be(&raw[d->data_len], crc);

  /* 写入底层存储 */
  sc = g_driver->write(d->nvm_key, raw, d->total_len);

  if (sc == SL_STATUS_OK) {
    g_cache[task.item].dirty = false;
  }
  /* 写入失败: 保留 dirty 标记, 下次 write() 触发新任务时会重试 */

  /* 通知回调 */
  if (task.callback != NULL) {
    task.callback(task.item, sc);
  }

  return !queue_is_empty();
}

/* =================================================================== */
/* delete(): 立即置无效 + 异步擦除                                       */
/* =================================================================== */

sl_status_t user_eeprom_delete(user_eeprom_item_t item)
{
  if (item >= EEPROM_ITEM_COUNT) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  g_cache[item].valid = false;
  g_cache[item].dirty = false;

  return g_driver->erase(g_desc[item].nvm_key);
}

/* =================================================================== */
/* 查询                                                                 */
/* =================================================================== */

bool user_eeprom_is_valid(user_eeprom_item_t item)
{
  if (item >= EEPROM_ITEM_COUNT) return false;
  return g_cache[item].valid;
}

uint16_t user_eeprom_get_data_len(user_eeprom_item_t item)
{
  if (item >= EEPROM_ITEM_COUNT) return 0U;
  return g_desc[item].data_len;
}

uint32_t user_eeprom_get_nvm_key(user_eeprom_item_t item)
{
  if (item >= EEPROM_ITEM_COUNT) return 0xFFFFFFFFU;
  return g_desc[item].nvm_key;
}

bool user_eeprom_is_write_pending(void)
{
  return !queue_is_empty();
}

/* =================================================================== */
/* flush(): 同步排空全部写队列                                           */
/* =================================================================== */

void user_eeprom_flush(void)
{
  write_task_t task;
  while (queue_pop(&task)) {
    if (!g_cache[task.item].dirty) continue;

    const eeprom_desc_t *d = &g_desc[task.item];
    uint8_t raw[EEPROM_MAX_TOTAL_LEN];
    uint16_t crc = eeprom_crc16(task.data, d->data_len);

    memcpy(raw, task.data, d->data_len);
    write_u16_be(&raw[d->data_len], crc);

    sl_status_t sc = g_driver->write(d->nvm_key, raw, d->total_len);
    if (sc == SL_STATUS_OK) {
      g_cache[task.item].dirty = false;
    }

    if (task.callback != NULL) {
      task.callback(task.item, sc);
    }
  }
}

/* =================================================================== */
/* factory_reset(): 删除非保留条目                                       */
/* =================================================================== */

void user_eeprom_factory_reset(void)
{
  /* 先落盘所有待处理写入 */
  user_eeprom_flush();

  for (uint16_t i = 0U; i < (uint16_t)EEPROM_ITEM_COUNT; i++) {
    /* 跳过保留条目 */
    if ((g_desc[i].flags & EEPROM_FLAG_PRESERVE) != 0U) continue;

    (void)g_driver->erase(g_desc[i].nvm_key);
    g_cache[i].valid = false;
    g_cache[i].dirty = false;
  }
}
