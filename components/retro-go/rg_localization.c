#include "rg_system.h"

static int rg_language = RG_LANG_EN;

static const char *language_names[RG_LANG_MAX] = {
    [RG_LANG_EN] = "English",
    [RG_LANG_ZH] = "中文",
};

static const struct {
    const char *en;
    const char *zh;
} zh_translations[] = {
    {"Yes", "是"},
    {"No", "否"},
    {"OK", "确定"},
    {"Close", "关闭"},
    {"None", "无"},
    {"<None>", "<无>"},
    {"On", "开启"},
    {"Off", "关闭"},
    {"Show", "显示"},
    {"Hide", "隐藏"},
    {"Auto", "自动"},
    {"Center", "居中"},
    {"Paging", "分页"},
    {"Carousel", "轮播"},
    {"Browser", "浏览"},
    {"Options", "选项"},
    {"About Retro-Go", "关于 Retro-Go"},
    {"Initializing...", "初始化..."},
    {"Language", "语言"},
    {"Language changed!", "语言已更改！"},
    {"For these changes to take effect you must restart your device.\nrestart now?", "需要重启设备使设置生效。\n现在重启？"},
    {"Launcher options", "主菜单选项"},
    {"Emulator options", "模拟器选项"},
    {"Color theme", "颜色主题"},
    {"Preview", "预览"},
    {"Scroll mode", "滚动模式"},
    {"Start screen", "开始屏幕"},
    {"Hide tabs", "隐藏标签"},
    {"File server", "文件服务器"},
    {"Startup app", "启动应用"},
    {"Build CRC cache", "构建 CRC 缓存"},
    {"Check for updates", "检查更新"},
    {"SD Card Error", "SD 卡错误"},
    {"Storage mount failed.\nMake sure the card is FAT32.", "存储挂载失败。\n请确认 SD 卡为 FAT32。"},
    {"Welcome to Retro-Go!", "欢迎使用 Retro-Go！"},
    {"Place roms in folder: %s", "游戏放到文件夹：%s"},
    {"With file extension: %s", "文件扩展名：%s"},
    {"You can hide this tab in the menu", "可在菜单中隐藏此标签"},
    {"You have no %s games", "没有 %s 游戏"},
    {"File not found", "文件未找到"},
    {"Name", "名字"},
    {"Folder", "目录"},
    {"Size", "大小"},
    {"Delete file", "删除文件"},
    {"File properties", "文件属性"},
    {"Delete selected file?", "删除选中文件？"},
    {"Resume game", "继续游戏"},
    {"New game", "新游戏"},
    {"Del favorite", "删除收藏"},
    {"Add favorite", "添加收藏"},
    {"Delete save", "删除存档"},
    {"Properties", "属性"},
    {"Resume", "继续"},
    {"Delete save?", "删除存档？"},
    {"Delete sram file?", "删除 SRAM 文件？"},
    {"Scanning %s %d/%d", "扫描 %s %d/%d"},
    {"Select file", "选择文件"},
    {"Folder is empty.", "文件夹为空。"},
    {"Horiz", "水平"},
    {"Vert", "垂直"},
    {"Both", "全部"},
    {"Fit", "适应"},
    {"Full", "全屏"},
    {"Zoom", "缩放"},
    {"System activity", "系统活动"},
    {"Disk activity", "磁盘活动"},
    {"Low battery", "低电量"},
    {"Not connected", "未连接"},
    {"Connecting...", "正在连接..."},
    {"Disconnecting...", "正在断开..."},
    {"Error", "错误"},
    {"Success", "成功"},
    {"Delete", "删除"},
};

int rg_localization_get_language_id(void)
{
    return rg_language;
}

bool rg_localization_set_language_id(int language_id)
{
    if (language_id < 0 || language_id > RG_LANG_MAX - 1)
        return false;

    rg_language = language_id;
    return true;
}

const char *rg_gettext(const char *text)
{
    if (rg_language == 0 || text == NULL)
        return text; // If rg_language is english or text is NULL, we can return self

    if (rg_language == RG_LANG_ZH)
    {
        for (size_t i = 0; i < RG_COUNT(zh_translations); ++i)
        {
            if (strcmp(zh_translations[i].en, text) == 0)
                return zh_translations[i].zh;
        }
    }

    return text; // if no translation found
}

const char *rg_localization_get_language_name(int language_id)
{
    if (language_id < 0 || language_id > RG_LANG_MAX - 1)
        return NULL;
    return language_names[language_id];
}
