/**
 * @file menu.h
 * @brief 参考工程兼容的Bootloader串口命令和状态码。
 */
#ifndef BOOT_MENU_H
#define BOOT_MENU_H

#include <stdint.h>

/**
 * @brief Bootloader上电启动模式开关。
 * @note 设为0：上电停留在Bootloader菜单，由SNTool写SN、检查或跳转APP。
 *       设为1：APP1有效时上电直接跳转APP；APP1无效时仍进入菜单。
 *       开发阶段需要跳过SNTool时只修改此处，不再依赖按键、SN或构建类型。
 */
#define BOOT_POWER_ON_JUMP_APP (1U)

#define BOOT_SYS_JUMP_APP     (0xF000U)
#define BOOT_SYS_RECOVERING   (0xF001U)
#define BOOT_SYS_RECOVER_FAIL (0xF002U)
#define BOOT_DNL_SUCCESS      (0xF003U)
#define BOOT_DNL_SIZE_OVER    (0xF004U)
#define BOOT_DNL_WRITE_ERR    (0xF005U)
#define BOOT_DNL_USER_ABORT   (0xF006U)
#define BOOT_DNL_RECV_ERR     (0xF007U)
#define BOOT_DNL_IMG_INVALID  (0xF008U)
#define BOOT_DNL_IMG_VALID    (0xF009U)
#define BOOT_ERASE_IMG_OK     (0xF00AU)
#define BOOT_ERASE_IMG_ERR    (0xF00BU)
#define BOOT_APP_COPY_OK      (0xF00CU)
#define BOOT_APP_COPY_ERR     (0xF00DU)
#define BOOT_MENU_ENTERED     (0xF00EU)
#define BOOT_WAIT_USER_CMD    (0xF00FU)
#define BOOT_USER_CMD_ERR     (0xF010U)
#define BOOT_WAIT_APP_DATA    (0xF011U)
#define BOOT_RESERVED_1       (0xF012U)
#define BOOT_SN_WAIT_DATA     (0xF013U)
#define BOOT_SN_RECV_ERR      (0xF014U)
#define BOOT_SN_EXIST         (0xF015U)
#define BOOT_SN_WRITE_OK      (0xF016U)
#define BOOT_SN_WRITE_ERR     (0xF017U)

void Main_Menu(void);
void Serial_ProgramSN(void);
void Boot_JumpToApp(void);

#endif /* BOOT_MENU_H */
