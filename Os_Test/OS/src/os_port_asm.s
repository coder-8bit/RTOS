    .syntax unified
    .cpu    cortex-m3
    .thumb

/* =========================================================================
 *  os_port_asm.s
 *  - Đây là phần "đụng CPU thật" của hệ thống.
 *  - Mọi quyết định task nào chạy tiếp đều đã được kernel C chuẩn bị xong.
 *  - File assembly này chỉ thực hiện đúng 3 đường đi quan trọng:
 *      1. SysTick_Handler  : gọi lại kernel để xử lý tick
 *      2. SVC_Handler      : launch task đầu tiên
 *      3. PendSV_Handler   : save/restore context giữa hai task
 *
 *  Liên kết với phần C:
 *    extern volatile TCB_t *g_current;      // TCB đang chạy; field đầu tiên là con trỏ stack (sp)
 *    extern volatile TCB_t *g_next;         // TCB được scheduler chọn (nếu khác NULL)
 *    extern void os_on_tick(void);          // Callback tick của kernel
 *    extern uint32_t *os_get_task_stack_ptr_for_switch(TCB_t *task);
 *
 *  Quy ước khung stack của một task (PSP, tăng địa chỉ lên trên):
 *    ... [địa chỉ thấp] ...
 *      R4, R5, R6, R7, R8, R9, R10, R11       (8 word)  <-- SW-saved frame (do phần mềm PUSH/POP)
 *      R0, R1, R2, R3, R12, LR, PC, xPSR      (8 word)  <-- HW-saved frame (do phần cứng tự PUSH/POP)
 *    ... [địa chỉ cao] ...
 *
 *  Quy ước con trỏ sp trong TCB:
 *    - current->sp luôn trỏ TỚI VỊ TRÍ R4 trong SW-frame của task hiện hành (tức là &R4).
 *    - Khi RESTORE, LDMIA r0!, {r4-r11} sẽ kéo r0 tiến tới &R0 (đầu HW-frame),
 *      sau đó MSR psp, r0 sẽ đặt PSP = &R0. Khi thoát handler bằng EXC_RETURN,
 *      phần cứng sẽ tự POP HW-frame (R0..xPSR) từ PSP → quay về Thread mode/PSP.
 * ========================================================================= */

    .extern g_current
    .extern g_next
    .extern os_on_tick
    .extern os_get_task_stack_ptr_for_switch

    .global PendSV_Handler
    .global SysTick_Handler
    .global SVC_Handler

/* =========================================================================
 * PendSV_Handler — ĐỔI NGỮ CẢNH (context switch)
 *
 * Mục tiêu:
 *  1) Nếu có g_next:
 *     - SAVE task cũ
 *     - current = next
 *     - consume g_next
 *     - lấy stack ptr hợp lệ của task mới
 *     - RESTORE task mới
 *  2) Nếu KHÔNG có g_next:
 *     - không được "tự ý" làm gì thêm
 *     - thoát ngay để CPU tiếp tục task cũ
 *
 * Lưu ý:
 *  - Trong Handler mode, LR giữ EXC_RETURN. BX LR sẽ kích hoạt logic
 *    “exception return”: phần cứng tự POP HW-frame từ PSP (nếu EXC_RETURN chọn PSP),
 *    và chuyển về Thread mode/PSP tiếp tục task.
 * ========================================================================= */
    .thumb_func
PendSV_Handler:
    /* [B1] Kiểm tra có task kế tiếp không (g_next != NULL) */
    LDR     r1, =g_next           /* r1 = &g_next */
    LDR     r2, [r1]              /* r2 = g_next (TCB*) */
    CBZ     r2, pend_exit         /* nếu r2 == 0 → không có next, thoát handler */

    /* [B2] Lưu ngữ cảnh task hiện hành vào PSP (nếu PSP hợp lệ) */
    MRS     r0, psp               /* r0 = PSP hiện tại (trỏ &R0 nếu chưa SAVE SW-frame) */
    CBZ     r0, pend_no_save      /* nếu PSP = 0 (chưa chạy task nào) → bỏ qua SAVE */

    /* PUSH {r4-r11} xuống stack của task hiện hành:
     *  - STMDB r0!, {r4-r11} giảm r0 rồi lưu: sau lệnh, r0 trỏ &R4 (đầu SW-frame).
     *  - Ghi nhớ: ta muốn current->sp = &R4.
     */
    STMDB   r0!, {r4-r11}

    /* current->sp = &R4 (giá trị r0 vừa cập nhật) */
    LDR     r3, =g_current        /* r3 = &g_current */
    LDR     r12, [r3]             /* r12 = g_current (TCB*) */
    STR     r0, [r12]             /* (*g_current).sp = r0 (= &R4) */

pend_no_save:
    /* [B3]
     * Từ thời điểm này, về mặt logic scheduler coi task mới là current.
     * g_next bị xóa để ngăn một đợt schedule khác ghi đè trong lúc restore.
     */
    LDR     r3, =g_current
    STR     r2, [r3]              /* g_current = g_next */
    MOVS    r3, #0
    STR     r3, [r1]              /* g_next = NULL */

    /* [B4]
     * Lấy stack ptr hợp lệ cho task kế tiếp.
     * Nếu task vừa được activate và context cũ không còn dùng được,
     * helper C sẽ dựng lại stack frame từ entry() trước khi trả về.
     */
    /* Giữ lại EXC_RETURN trong LR qua lệnh BL bên dưới.
     * Nếu không, BX lr ở cuối handler sẽ không còn quay về Thread/PSP đúng cách.
     */
    PUSH    {lr}
    MOV     r0, r2
    BL      os_get_task_stack_ptr_for_switch
    POP     {lr}

    /* [B5] Phục hồi SW-frame của next và cập nhật PSP:
     *  - LDMIA r0!, {r4-r11}: nạp R4..R11 từ vùng SW-frame của next,
     *    đồng thời r0 tiến tới &R0 (đầu HW-frame).
     *  - MSR psp, r0: đặt PSP = &R0 của next.
     *  Khi BX LR (exception return), phần cứng tự POP HW-frame của next
     *  (R0..xPSR) → nhảy về PC của next, tiếp tục chạy Thread mode/PSP.
     */
    LDMIA   r0!, {r4-r11}         /* pop SW-frame → r0 = &R0 (đầu HW-frame) */
    MSR     psp, r0               /* PSP = &R0 của next */

    /* Hàng rào bộ nhớ/điều khiển luồng để chắc chắn cập nhật PSP/ghi bộ nhớ được
     * nhìn thấy đúng trước khi rời Handler.
     */
    DSB
    ISB

pend_exit:
    /* [B6] Thoát handler.
     *  - Nếu có next: LR đang là EXC_RETURN (ví dụ 0xFFFFFFFD),
     *    BX LR sẽ exception return về Thread/PSP của next.
     *  - Nếu không có next: quay về task hiện hành như cũ.
     */
    BX      lr

/* =========================================================================
 * SysTick_Handler — NGẮT ĐỊNH KỲ 1ms
 *
 * Mục tiêu:
 *  - Gọi os_on_tick() để kernel xử lý tick/alarm/schedule.
 *  - Bảo toàn EXC_RETURN trong LR: vì BL sẽ ghi đè LR.
 *  - Không tự chạm vào logic scheduler ở assembly.
 *
 * Lưu ý:
 *  - R4..R11 là callee-saved theo AAPCS, hàm C phải bảo toàn nếu dùng.
 *  - HW-frame (R0..xPSR) đã được phần cứng tự lưu khi vào ngắt.
 * ========================================================================= */
    .thumb_func
SysTick_Handler:
    PUSH    {lr}                  /* lưu EXC_RETURN để BL không ghi đè */
    BL      os_on_tick       /* gọi callback tick 1ms của OS */
    POP     {lr}                  /* khôi phục EXC_RETURN về LR */
    BX      lr                    /* exception return về ngữ cảnh trước ngắt */

/* =========================================================================
 * SVC_Handler — KHỞI CHẠY TASK ĐẦU TIÊN
 *
 * Mục tiêu:
 *  - Dùng đúng một lần khi hệ thống rời main() để vào task đầu tiên.
 *  - Lấy stack ptr của g_current qua helper C để đảm bảo context đã sẵn sàng.
 *  - POP SW-frame (R4..R11) để r0 → &R0 (đầu HW-frame).
 *  - Thiết lập PSP = &R0; chọn dùng PSP trong Thread mode (CONTROL.SPSEL=1).
 *  - Thực hiện EXC_RETURN (0xFFFFFFFD) để về Thread mode, dùng PSP, POP HW-frame
 *    (R0..xPSR) và nhảy vào PC của task đầu tiên (đã cài trong HW-frame).
 * ========================================================================= */
    .thumb_func
SVC_Handler:
    /* [C1] r0 = stack ptr hợp lệ của task đầu tiên */
    LDR   r0, =g_current          /* r0 = &g_current */
    LDR   r0, [r0]                /* r0 = g_current (TCB*) */
    BL    os_get_task_stack_ptr_for_switch

    /* [C2] POP SW-frame → r0 tiến tới &R0 (đầu HW-frame) */
    LDMIA r0!, {r4-r11}           /* nạp lại R4..R11 của task đầu tiên */

    /* [C3] PSP = &R0 để chuẩn bị exception return */
    MSR   psp, r0                 /* PSP = &R0 (đầu HW-frame) */

    /* [C4] Chọn PSP cho Thread mode (CONTROL.SPSEL = 1), vẫn privileged (n bit = 0) */
    MOVS  r0, #2                  /* 0b10: SPSEL=1, nPRIV=0 */
    MSR   control, r0
    ISB                           /* đồng bộ pipeline sau khi đổi CONTROL */

    /* [C5]
     * EXC_RETURN = 0xFFFFFFFD:
     * - quay về Thread mode
     * - dùng PSP thay vì MSP
     * - hardware tự unstack HW-frame và nhảy vào PC của task
     */
    LDR   r0, =0xFFFFFFFD
    BX    r0                      /* phần cứng tự POP HW-frame → nhảy vào PC của task */
/* (Tùy thích) Khai báo kích thước symbol cho linker/map */
    .size PendSV_Handler, . - PendSV_Handler
    .size SysTick_Handler, . - SysTick_Handler
    .size SVC_Handler,     . - SVC_Handler
