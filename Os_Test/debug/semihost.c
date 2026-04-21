/**
 * @file    semihost.c
 * @brief   Semihosting output cho debug qua GDB/OpenOCD.
 * @details Cung cấp retarget _write() để printf()->semihosting.
 *          Chỉ hoạt động khi có debugger kết nối.
 *
 *          Ghi chú thực tế:
 *          - Semihosting rất tiện cho debug sớm.
 *          - Nhưng nó cũng có thể làm luồng test batch phức tạp hơn vì
 *            lệnh BKPT 0xAB sẽ tương tác trực tiếp với debugger.
 *          - Vì vậy file này luôn kiểm tra debugger trước khi gọi semihost.
 *
 * @version 1.0.0
 * @date    2024-12-11
 * @author  HALA Academy
 */

#include "os_config.h"
#include "stm32f10x.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @func    dbg_attached
 * @brief   Kiểm tra có debugger đang kết nối không.
 * @return  int 1 nếu có debugger, 0 nếu không.
 */
static inline int dbg_attached(void) {
  /*
   * DHCSR.C_DEBUGEN = 1 khi CPU đang nằm dưới điều khiển debug.
   * Nếu bit này = 0 mà vẫn gọi BKPT semihosting thì firmware có thể treo.
   */
  return (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0;
}

/**
 * @func    sh_write0
 * @brief   Semihosting SYS_WRITE0 - in chuỗi ra host.
 * @param[in] s Chuỗi kết thúc '\0'.
 */
static inline void sh_write0(const char *s) {
  /* Theo ARM semihosting ABI:
   * - r0 = mã service
   * - r1 = tham số của service
   * SYS_WRITE0 nhận một chuỗi C kết thúc bởi '\0'. */
  register uint32_t r0 __asm__("r0") = 0x04; /* SYS_WRITE0 */
  register const char *r1 __asm__("r1") = s; /* con trỏ tới chuỗi */
  __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
}

/**
 * @func    _write
 * @brief   Retarget printf -> semihosting.
 * @details Chỉ hoạt động nếu có debugger. Nếu chạy độc lập sẽ bỏ qua.
 * @param[in] fd   File descriptor (bỏ qua).
 * @param[in] buf  Buffer dữ liệu.
 * @param[in] len  Độ dài dữ liệu.
 * @return  int Số byte đã ghi.
 */
int _write(int fd, const void *buf, size_t len) {
  (void)fd;

  /*
   * Mặc định tắt semihosting để phiên GDB batch/CI không bị ngắt bởi BKPT 0xAB.
   * Khi cần debug tay bằng probe hỗ trợ semihosting, bật OS_ENABLE_SEMIHOSTING.
   */
#if !OS_ENABLE_SEMIHOSTING
  (void)buf;
  return (int)len;
#else
  /* Nếu không có debugger, coi như đã "ghi thành công" để app vẫn chạy tiếp. */
  if (!dbg_attached()) {
    return (int)len;
  }

  /*
   * Semihosting SYS_WRITE0 yêu cầu chuỗi kết thúc bởi '\0',
   * nên ta chép tạm buffer sang vùng line[] rồi chèn null-terminator.
   */
  static char line[256];
  size_t n = (len < sizeof(line) - 1) ? len : (sizeof(line) - 1);
  for (size_t i = 0; i < n; ++i) {
    line[i] = ((const char *)buf)[i];
  }
  line[n] = '\0';
  sh_write0(line);

  return (int)len;
#endif
}
