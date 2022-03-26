/* For OEM project information
 *such as project name, hardware ID
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/sys_soc.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/project_info.h>
#include <soc/qcom/smem.h>
#include <linux/gpio.h>
#include <soc/qcom/socinfo.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/machine.h>
#include <linux/pstore.h>

static struct component_info component_info_desc[COMPONENT_MAX];
static struct kobject *project_info_kobj;
static struct project_info *project_info_desc_v1 = NULL;
static struct project_info_v2 *project_info_desc_v2 = NULL;
static struct dump_info *dp_info;

static struct kobject *component_info;
static ssize_t project_info_get(struct device *dev,
    struct device_attribute *attr, char *buf);
static ssize_t component_info_get(struct device *dev,
    struct device_attribute *attr, char *buf);
static int op_aboard_read_gpio(void);

static DEVICE_ATTR(project_name, 0444, project_info_get, NULL);
static DEVICE_ATTR(hw_id, 0444, project_info_get, NULL);
static DEVICE_ATTR(rf_id_v1, 0444, project_info_get, NULL);
static DEVICE_ATTR(rf_id_v2, 0444, project_info_get, NULL);
static DEVICE_ATTR(rf_id_v3, 0444, project_info_get, NULL);
static DEVICE_ATTR(modem, 0444, project_info_get, NULL);
static DEVICE_ATTR(operator_no, 0444, project_info_get, NULL);
static DEVICE_ATTR(ddr_manufacture_info, 0444, project_info_get, NULL);
static DEVICE_ATTR(ddr_row, 0444, project_info_get, NULL);
static DEVICE_ATTR(ddr_column, 0444, project_info_get, NULL);
static DEVICE_ATTR(ddr_fw_version, 0444, project_info_get, NULL);
static DEVICE_ATTR(ddr_reserve_info, 0444, project_info_get, NULL);
static DEVICE_ATTR(secboot_status, 0444, project_info_get, NULL);
static DEVICE_ATTR(platform_id, 0444, project_info_get, NULL);
static DEVICE_ATTR(serialno, 0444, project_info_get, NULL);
static DEVICE_ATTR(feature_id, 0444, project_info_get, NULL);
static DEVICE_ATTR(aboard_id, 0444, project_info_get, NULL);

#define GET_PROJECT_INFO(member) (project_info_desc_v2 ? project_info_desc_v2->member : project_info_desc_v1->member)
#define SET_PROJECT_INFO(member, value) \
    if (project_info_desc_v2) \
        project_info_desc_v2->member = value; \
    else \
        project_info_desc_v1->member = value;

void save_dump_reason_to_smem(char *info, char *function_name)
{
    int strl = 0, strl1 = 0;
    static int flag = 0;

    /* Make sure save_dump_reason_to_smem() is not called
     infinite times by panic()  during DDR bit flip crash etc */
    if (flag > 1)
	return;

    dp_info = smem_alloc(SMEM_DUMP_INFO,
        sizeof(struct dump_info), 0,
        SMEM_ANY_HOST_FLAG);

    if (IS_ERR_OR_NULL(dp_info))
        pr_err("%s: get dp_info failure\n", __func__);
    else {
        pr_err("%s: info : %s\n",__func__, info);

        strl = strlen(info)+1;
        strl1 = strlen(function_name)+1;
        strl = strl <  DUMP_REASON_SIZE ? strl: DUMP_REASON_SIZE;
        strl1 = strl1 <  DUMP_REASON_SIZE ? strl1: DUMP_REASON_SIZE ;
        if ((strlen(dp_info->dump_reason) + strl) < DUMP_REASON_SIZE)
                strncat(dp_info->dump_reason,info,strl);

        if (function_name != NULL && ((strlen(dp_info->dump_reason) + strl1) < DUMP_REASON_SIZE)) {
                strncat(dp_info->dump_reason,function_name,strl1);
                strncat(dp_info->dump_reason,"\n",1);
	}
    }
    pr_err("\r%s: dump_reason : %s strl=%d function caused panic :%s strl1=%d \n", __func__,
                           dp_info->dump_reason, strl, function_name, strl1);
    //save_dump_reason_to_device_info(dp_info->dump_reason);
    flag++;
}

uint8 get_secureboot_fuse_status(void)
{
    void __iomem *oem_config_base;
    uint8 secure_oem_config = 0;

    oem_config_base = ioremap(SECURE_BOOT1, 1);
    if (!oem_config_base)
        return -EINVAL;
    secure_oem_config = __raw_readb(oem_config_base);
    iounmap(oem_config_base);
    pr_debug("secure_oem_config 0x%x\n", secure_oem_config);

    return secure_oem_config;
}

static ssize_t project_info_get(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
    if (project_info_desc_v1 || project_info_desc_v2) {
        if (attr == &dev_attr_project_name)
            return snprintf(buf, BUF_SIZE, "%s\n",
            GET_PROJECT_INFO(project_name));
        if (attr == &dev_attr_hw_id)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(hw_version));
        if (attr == &dev_attr_rf_id_v1)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(rf_v1));
        if (attr == &dev_attr_rf_id_v2)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(rf_v2));
        if (attr == &dev_attr_rf_id_v3)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(rf_v3));
        if (attr == &dev_attr_modem)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(modem));
        if (attr == &dev_attr_operator_no)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(operator));
        if (attr == &dev_attr_ddr_manufacture_info)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(ddr_manufacture_info));
        if (attr == &dev_attr_ddr_row)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(ddr_row));
        if (attr == &dev_attr_ddr_column)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(ddr_column));
        if (attr == &dev_attr_ddr_fw_version)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(ddr_fw_version));
        if (attr == &dev_attr_ddr_reserve_info)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(ddr_reserve_info));
        if (attr == &dev_attr_secboot_status)
            return snprintf(buf, BUF_SIZE, "%d\n",
            get_secureboot_fuse_status());
        if (attr == &dev_attr_platform_id)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(platform_id));

        if (attr == &dev_attr_serialno)
            return snprintf(buf, BUF_SIZE, "0x%x\n",
            socinfo_get_serial_number());

        if (attr == &dev_attr_feature_id)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(feature_id));

        if (attr == &dev_attr_aboard_id)
            return snprintf(buf, BUF_SIZE, "%d\n",
            GET_PROJECT_INFO(a_board_version));
    }

    return -EINVAL;
}

static struct attribute *project_info_sysfs_entries[] = {
    &dev_attr_project_name.attr,
    &dev_attr_hw_id.attr,
    &dev_attr_rf_id_v1.attr,
    &dev_attr_rf_id_v2.attr,
    &dev_attr_rf_id_v3.attr,
    &dev_attr_modem.attr,
    &dev_attr_operator_no.attr,
    &dev_attr_ddr_manufacture_info.attr,
    &dev_attr_ddr_row.attr,
    &dev_attr_ddr_column.attr,
    &dev_attr_ddr_fw_version.attr,
    &dev_attr_ddr_reserve_info.attr,
    &dev_attr_secboot_status.attr,
    &dev_attr_platform_id.attr,
    &dev_attr_serialno.attr,
    &dev_attr_feature_id.attr,
    &dev_attr_aboard_id.attr,
    NULL,
};

static struct attribute_group project_info_attr_group = {
    .attrs  = project_info_sysfs_entries,
};

static DEVICE_ATTR(ddr, 0444, component_info_get, NULL);
static DEVICE_ATTR(emmc, 0444, component_info_get, NULL);
static DEVICE_ATTR(f_camera, 0444, component_info_get, NULL);
static DEVICE_ATTR(r_camera, 0444, component_info_get, NULL);
static DEVICE_ATTR(second_r_camera, 0444, component_info_get, NULL);
static DEVICE_ATTR(ois, 0444, component_info_get, NULL);
static DEVICE_ATTR(tp, 0444, component_info_get, NULL);
static DEVICE_ATTR(lcd, 0444, component_info_get, NULL);
static DEVICE_ATTR(wcn, 0444, component_info_get, NULL);
static DEVICE_ATTR(l_sensor, 0444, component_info_get, NULL);
static DEVICE_ATTR(g_sensor, 0444, component_info_get, NULL);
static DEVICE_ATTR(m_sensor, 0444, component_info_get, NULL);
static DEVICE_ATTR(gyro, 0444, component_info_get, NULL);
static DEVICE_ATTR(backlight, 0444, component_info_get, NULL);
static DEVICE_ATTR(mainboard, 0444, component_info_get, NULL);
static DEVICE_ATTR(fingerprints, 0444, component_info_get, NULL);
static DEVICE_ATTR(touch_key, 0444, component_info_get, NULL);
static DEVICE_ATTR(ufs, 0444, component_info_get, NULL);
static DEVICE_ATTR(Aboard, 0444, component_info_get, NULL);
static DEVICE_ATTR(nfc, 0444, component_info_get, NULL);
static DEVICE_ATTR(fast_charge, 0444, component_info_get, NULL);
static DEVICE_ATTR(cpu, 0444, component_info_get, NULL);
static DEVICE_ATTR(rf_version, 0444, component_info_get, NULL);


char *get_component_version(enum COMPONENT_TYPE type)
{
    if (type >= COMPONENT_MAX) {
        pr_err("%s == type %d invalid\n", __func__, type);
        return "N/A";
    }
    return component_info_desc[type].version?:"N/A";
}

char *get_component_manufacture(enum COMPONENT_TYPE type)
{
    if (type >= COMPONENT_MAX) {
        pr_err("%s == type %d invalid\n", __func__, type);
        return "N/A";
    }
    return component_info_desc[type].manufacture?:"N/A";

}

int push_component_info(enum COMPONENT_TYPE type,
    char *version, char *manufacture)
{
    if (type >= COMPONENT_MAX)
        return -ENOMEM;
    component_info_desc[type].version = version;
    component_info_desc[type].manufacture = manufacture;

    return 0;
}
EXPORT_SYMBOL(push_component_info);

int reset_component_info(enum COMPONENT_TYPE type)
{
    if (type >= COMPONENT_MAX)
        return -ENOMEM;
    component_info_desc[type].version = NULL;
    component_info_desc[type].manufacture = NULL;

    return 0;
}
EXPORT_SYMBOL(reset_component_info);


static struct attribute *component_info_sysfs_entries[] = {
    &dev_attr_ddr.attr,
    &dev_attr_emmc.attr,
    &dev_attr_f_camera.attr,
    &dev_attr_r_camera.attr,
    &dev_attr_second_r_camera.attr,
    &dev_attr_ois.attr,
    &dev_attr_tp.attr,
    &dev_attr_lcd.attr,
    &dev_attr_wcn.attr,
    &dev_attr_l_sensor.attr,
    &dev_attr_g_sensor.attr,
    &dev_attr_m_sensor.attr,
    &dev_attr_gyro.attr,
    &dev_attr_backlight.attr,
    &dev_attr_mainboard.attr,
    &dev_attr_fingerprints.attr,
    &dev_attr_touch_key.attr,
    &dev_attr_ufs.attr,
    &dev_attr_Aboard.attr,
    &dev_attr_nfc.attr,
    &dev_attr_fast_charge.attr,
    &dev_attr_cpu.attr,
    &dev_attr_rf_version.attr,
    NULL,
};

static struct attribute_group component_info_attr_group = {
    .attrs  = component_info_sysfs_entries,
};

static ssize_t component_info_get(struct device *dev,
                struct device_attribute *attr,
                char *buf)
{
    if (attr == &dev_attr_ddr)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(DDR),
        get_component_manufacture(DDR));
    if (attr == &dev_attr_emmc)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(EMMC),
        get_component_manufacture(EMMC));
    if (attr == &dev_attr_f_camera)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(F_CAMERA),
        get_component_manufacture(F_CAMERA));
    if (attr == &dev_attr_r_camera)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(R_CAMERA),
        get_component_manufacture(R_CAMERA));
    if (attr == &dev_attr_second_r_camera)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(SECOND_R_CAMERA),
        get_component_manufacture(SECOND_R_CAMERA));
    if (attr == &dev_attr_ois)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(OIS),
        get_component_manufacture(OIS));
    if (attr == &dev_attr_tp)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(TP),
        get_component_manufacture(TP));
    if (attr == &dev_attr_lcd)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(LCD),
        get_component_manufacture(LCD));
    if (attr == &dev_attr_wcn)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(WCN),
        get_component_manufacture(WCN));
    if (attr == &dev_attr_l_sensor)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(I_SENSOR),
        get_component_manufacture(I_SENSOR));
    if (attr == &dev_attr_g_sensor)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(G_SENSOR),
        get_component_manufacture(G_SENSOR));
    if (attr == &dev_attr_m_sensor)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(M_SENSOR),
        get_component_manufacture(M_SENSOR));
    if (attr == &dev_attr_gyro)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(GYRO),
        get_component_manufacture(GYRO));
    if (attr == &dev_attr_backlight)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(BACKLIGHT),
        get_component_manufacture(BACKLIGHT));
    if (attr == &dev_attr_mainboard)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(MAINBOARD),
        get_component_manufacture(MAINBOARD));
    if (attr == &dev_attr_fingerprints)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(FINGERPRINTS),
        get_component_manufacture(FINGERPRINTS));
    if (attr == &dev_attr_touch_key)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(TOUCH_KEY),
        get_component_manufacture(TOUCH_KEY));
    if (attr == &dev_attr_ufs)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(UFS),
        get_component_manufacture(UFS));
    if (attr == &dev_attr_Aboard) {
        //op_aboard_read_gpio();
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(ABOARD),
        get_component_manufacture(ABOARD));
    }
    if (attr == &dev_attr_nfc)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(NFC),
        get_component_manufacture(NFC));
    if (attr == &dev_attr_fast_charge)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(FAST_CHARGE),
        get_component_manufacture(FAST_CHARGE));
    if (attr == &dev_attr_cpu)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(CPU),
        get_component_manufacture(CPU));
    if (attr == &dev_attr_rf_version)
        return snprintf(buf, BUF_SIZE, "VER:\t%s\nMANU:\t%s\n",
        get_component_version(RF_VERSION),
        get_component_manufacture(RF_VERSION));
    return -EINVAL;
}

static int __init project_info_init_sysfs(void)
{
    int error = 0;

    project_info_kobj = kobject_create_and_add("project_info", NULL);
    if (!project_info_kobj)
        return -ENOMEM;
    error = sysfs_create_group(project_info_kobj, &project_info_attr_group);
    if (error) {
        pr_err("project_info_init_sysfs project_info_attr_group failure\n");
        return error;
    }

    component_info = kobject_create_and_add("component_info",
        project_info_kobj);
    pr_info("project_info_init_sysfs success\n");
    if (!component_info)
        return -ENOMEM;

    error = sysfs_create_group(component_info, &component_info_attr_group);
    if (error) {
        pr_err("project_info_init_sysfs project_info_attr_group failure\n");
        return error;
    }
    return 0;
}

late_initcall(project_info_init_sysfs);

struct ddr_manufacture {
    int id;
    char name[20];
};
//ddr id and ddr name
static char ddr_version[32] = {0};
static char ddr_manufacture[20] = {0};
char ddr_manufacture_and_fw_verion[40] = {0};
static char cpu_type[20] = {0};

struct ddr_manufacture ddr_manufacture_list[] = {
    {1, "Samsung "},
    {2, "Qimonda "},
    {3, "Elpida "},
    {4, "Etpon "},
    {5, "Nanya "},
    {6, "Hynix "},
    {7, "Mosel "},
    {8, "Winbond "},
    {9, "Esmt "},
    {255, "Micron"},
    {0, "Unknown"},
};

struct cpu_list {
    int id;
    char name[20];
};

struct cpu_list cpu_list_msm[] = {
    {194, "MSM8974AC "},
    {217, "MSM8974AA "},
    {218, "MSM8974AB "},
    {207, "MSM8994 "},
    {246, "MSM8996 "},
    {292, "MSM8998 "},
    {302, "MSM8996L "},
    {305, "MSM8996SG "},
    {310, "MSM8996AU "},
    {321, "SDM845 "},
    {0, "Unknown"},
};

void get_ddr_manufacture_name(void)
{
    uint32 i, length;

    length = ARRAY_SIZE(ddr_manufacture_list);
    if (project_info_desc_v1 || project_info_desc_v2) {
        for (i = 0; i < length; i++) {
            if (ddr_manufacture_list[i].id ==
                GET_PROJECT_INFO(ddr_manufacture_info)) {
                snprintf(ddr_manufacture, sizeof(ddr_manufacture), "%s",
                    ddr_manufacture_list[i].name);
                break;
            }
        }
    }
}

void get_cpu_type(void)
{
    uint32 i, length;

    length = ARRAY_SIZE(cpu_list_msm);
    if (project_info_desc_v1 || project_info_desc_v2) {
        for (i = 0; i < length; i++) {
            if (cpu_list_msm[i].id ==
                GET_PROJECT_INFO(platform_id)) {
                snprintf(cpu_type, sizeof(cpu_type),
                    "%s", cpu_list_msm[i].name);
                break;
            }
        }
    }
}

static char mainboard_version[16] = {0};
static char mainboard_manufacture[8] = {'O',
    'N', 'E', 'P', 'L', 'U', 'S', '\0'};
static char Aboard_version[16] = {0};
static char rf_version[16] = {0};

struct a_borad_version{

       int  version;
       char   name[16];
};

struct a_borad_version a_borad_version_string_arry[]={

    {1,   "TMO"},
    {2,   "US/EU" },
    {3,   "CN/IN"},
    {4,   "Unknown"},
    {5,   "Unknown" },
};


struct a_borad_version a_borad_version_string_arry_gpio[]={

    {0,   "TMO"},
    {1,   "US/EU/TMO" },
    {2,   "CN/IN"},
    {3,   "Unknown"},
    {4,   "Unknown" },
    {5,   "Unknown"},
    
};

uint32 get_hw_version(void)
{
    if (strnstr(saved_command_line, "androidboot.platform_name=", strlen(saved_command_line)))
        project_info_desc_v2 = smem_find(SMEM_PROJECT_INFO,
                    sizeof(struct project_info_v2),
                    0,
                    SMEM_ANY_HOST_FLAG);
    else
        project_info_desc_v1 = smem_find(SMEM_PROJECT_INFO,
                    sizeof(struct project_info),
                    0,
                    SMEM_ANY_HOST_FLAG);

    if (IS_ERR_OR_NULL(project_info_desc_v1) && IS_ERR_OR_NULL(project_info_desc_v2))
        pr_err("%s: get project_info failure\n", __func__);
    else {
        pr_err("%s: hw version: %d\n", __func__,
            GET_PROJECT_INFO(hw_version));
        return GET_PROJECT_INFO(hw_version);
    }
    return 0;
}

int __init init_project_info(void)
{
    static bool project_info_init_done;
    int ddr_size = 0;

    if (project_info_init_done)
        return 0;

    if (strnstr(saved_command_line, "androidboot.platform_name=", strlen(saved_command_line)))
        project_info_desc_v2 = smem_find(SMEM_PROJECT_INFO,
                    sizeof(struct project_info_v2),
                    0,
                    SMEM_ANY_HOST_FLAG);
    else
        project_info_desc_v1 = smem_find(SMEM_PROJECT_INFO,
                    sizeof(struct project_info),
                    0,
                    SMEM_ANY_HOST_FLAG);

    if (IS_ERR_OR_NULL(project_info_desc_v1) && IS_ERR_OR_NULL(project_info_desc_v2)) {
        pr_err("%s: get project_info failure\n", __func__);
        return 0;
    }
    pr_err("%s: project_name: %s hw_version: %d rf_v1: %d rf_v2: %d: rf_v3: %d  paltform_id:%d\n",
        __func__, GET_PROJECT_INFO(project_name),
        GET_PROJECT_INFO(hw_version),
        GET_PROJECT_INFO(rf_v1),
        GET_PROJECT_INFO(rf_v2),
        GET_PROJECT_INFO(rf_v3),
        GET_PROJECT_INFO(platform_id));

    switch (GET_PROJECT_INFO(hw_version)) {
    case 11:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "EVB");
        break;
    case 12:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "T0");
        break;
    case 13:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "T1");
        break;
    case 14:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "EVT1");
        break;
    case 15:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "EVT2");
        break;
    case 21:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "DVT");
        break;
    case 22:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "PVT");
        break;
    case 23:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "PVT");
        break;
    case 24:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "PVT_MCU");
        break;
    case 25:
    snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "DVTBACKUP");
        break;

    case 31:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "EVB");
        break;
    case 32:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "T0");
        break;
    case 33:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "EVT1");
        break;
    case 34:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "DVT");
        break;
    case 35:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "2ND");
        break;
    case 41:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "PVT");
        break;
    case 42:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "PVT2nd");
        break;
    case 43:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "PVT-1");
        break;
    case 44:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "PVTSpec");
        break;
    case 45:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "MPSpec");
        break;
    case 55:
        snprintf(mainboard_version, sizeof(mainboard_version), "%s %s",
        GET_PROJECT_INFO(project_name), "DVTUSB30");
        break;

    default:
        snprintf(mainboard_version, sizeof(mainboard_version), "%d",
        GET_PROJECT_INFO(hw_version));
        break;
    }
    push_component_info(MAINBOARD,
        mainboard_version,
        mainboard_manufacture);

   if( GET_PROJECT_INFO(hw_version) <= 32 ) {
        snprintf(Aboard_version, sizeof(Aboard_version), "%d %s",
        GET_PROJECT_INFO(a_board_version),GET_PROJECT_INFO(a_board_version) <=5 ?
        a_borad_version_string_arry[GET_PROJECT_INFO(a_board_version) -1].name:"Unknown");

        push_component_info(ABOARD, Aboard_version, mainboard_manufacture);
        pr_err("%s: Aboard_gpio(%s)\n", __func__, Aboard_version);

    }

    snprintf(rf_version, sizeof(rf_version),  " %d",GET_PROJECT_INFO(rf_v1));
	push_component_info(RF_VERSION, rf_version, mainboard_manufacture);

	get_ddr_manufacture_name();
	if (totalram_pages > 9*(1<<18))
		ddr_size = 10;
	else if (totalram_pages > 6*(1<<18))
		ddr_size = 8;
	else if (totalram_pages > 5*(1<<18))
		ddr_size = 6;
	else if (totalram_pages > 4*(1<<18))
		ddr_size = 5;
	else if (totalram_pages > 3*(1<<18))
		ddr_size = 4;
	else if (totalram_pages > 2*(1<<18))
		ddr_size = 3;
	else if (totalram_pages > 1*(1<<18))
		ddr_size = 2;

    snprintf(ddr_version, sizeof(ddr_version), "size_%dG_r_%d_c_%d",
        ddr_size, GET_PROJECT_INFO(ddr_row),
        GET_PROJECT_INFO(ddr_column));
    snprintf(ddr_manufacture_and_fw_verion,
        sizeof(ddr_manufacture_and_fw_verion),
        "%s%s %u.%u", ddr_manufacture,
        GET_PROJECT_INFO(ddr_reserve_info) == 0x05 ? "20nm" :
        (GET_PROJECT_INFO(ddr_reserve_info) == 0x06 ? "18nm" : " "),
        GET_PROJECT_INFO(ddr_fw_version) >> 16,
        GET_PROJECT_INFO(ddr_fw_version) & 0x0000FFFF);
    push_component_info(DDR, ddr_version, ddr_manufacture_and_fw_verion);

    get_cpu_type();
    push_component_info(CPU, cpu_type, "Qualcomm");
    project_info_init_done = true;

    return 0;
}
struct aborad_data {
    int aboard_gpio_0;//gpio33
    int aboard_gpio_1;//gpio34
    struct pinctrl                      *pinctrl;
    struct pinctrl_state                *pinctrl_state_active;
    struct pinctrl_state                *pinctrl_state_suspend;

    struct device *dev;
};
static struct aborad_data *data = NULL ;

static int op_aboard_request_named_gpio(const char *label, int *gpio)
{
    struct device *dev = data->dev;
    struct device_node *np = dev->of_node;
    int rc = of_get_named_gpio(np, label, 0);

    if (rc < 0) {
        dev_err(dev, "failed to get '%s'\n", label);
        *gpio = rc;
        return rc;
    }
    *gpio = rc;
/*
    rc = devm_gpio_request(dev, *gpio, label);
    if (rc) {
        dev_err(dev, "failed to request gpio %d\n", *gpio);
        return rc;
    }
*/
    dev_info(dev, "%s - gpio: %d\n", label, *gpio);
    return 0;
}

static int op_aboard_read_gpio(void)
{
    int gpio0 = 0;
    int gpio1 = 0;
    if ( data == NULL )
    {
        return 0 ;
    }
    gpio0 = gpio_get_value(data->aboard_gpio_0);
    gpio1 = gpio_get_value(data->aboard_gpio_1);

    pr_err("%s: gpio0=%d gpio1=%d\n", __func__, gpio0,gpio1);

    if( gpio0 == 0 && gpio1 == 0 )
    {
        SET_PROJECT_INFO(a_board_version, 0);
    }
    else if( gpio0 == 0 && gpio1 == 1 )
    {
        SET_PROJECT_INFO(a_board_version, 1);
    }
    else if( gpio0 == 1 && gpio1 == 0 )
    {
        SET_PROJECT_INFO(a_board_version, 2);
    }
    else
    {
        SET_PROJECT_INFO(a_board_version, -1);
    }
    snprintf(Aboard_version, sizeof(Aboard_version), "%d %s",
    GET_PROJECT_INFO(a_board_version),GET_PROJECT_INFO(a_board_version) <3 ?
    a_borad_version_string_arry_gpio[GET_PROJECT_INFO(a_board_version)].name:"Unknown");

    push_component_info(ABOARD, Aboard_version, mainboard_manufacture);
    pr_err("%s: Aboard_gpio(%s)\n", __func__, Aboard_version);
    return 0 ;

}

static int oem_aboard_probe(struct platform_device *pdev)
{
    int rc = 0;
    struct device *dev = &pdev->dev;

    data = kzalloc(sizeof(struct aborad_data), GFP_KERNEL);
    if (!data) {
        pr_err("%s: failed to allocate memory\n", __func__);
        rc = -ENOMEM;
        goto exit;
    }

    data->dev = dev;

    rc = op_aboard_request_named_gpio("oem,aboard-gpio-0",&data->aboard_gpio_0);
    if (rc) {
        pr_err("%s: op_rf_request_named_gpio gpio-0 fail\n",
            __func__);
        goto exit_gpio;
    }
    rc = op_aboard_request_named_gpio("oem,aboard-gpio-1",&data->aboard_gpio_1);
    if (rc) {
        pr_err("%s: op_rf_request_named_gpio gpio-1 fail\n",
            __func__);
        goto exit_gpio;
    }
    
    data->pinctrl = devm_pinctrl_get((data->dev));
    if (IS_ERR_OR_NULL(data->pinctrl)) {
        rc = PTR_ERR(data->pinctrl);
        pr_err("%s pinctrl error!\n",__func__);
        goto err_pinctrl_get;
    }

    data->pinctrl_state_active = pinctrl_lookup_state(data->pinctrl, "oem_aboard_active");

    if (IS_ERR_OR_NULL(data->pinctrl_state_active)) {
        rc = PTR_ERR(data->pinctrl_state_active);
        pr_err("%s pinctrl state active error!\n",__func__);
        goto err_pinctrl_lookup;
    }

    if (data->pinctrl) {
        rc = pinctrl_select_state(data->pinctrl,data->pinctrl_state_active);
    }

    gpio_direction_input(data->aboard_gpio_0);
    gpio_direction_input(data->aboard_gpio_1);
    op_aboard_read_gpio();
    pr_err("%s: probe ok!\n", __func__);
    return 0;
    
err_pinctrl_lookup:
    devm_pinctrl_put(data->pinctrl);
err_pinctrl_get:
    data->pinctrl = NULL;
exit_gpio:
    kfree(data);
exit:
    pr_err("%s: probe Fail!\n", __func__);

    return rc;
}

static const struct of_device_id aboard_of_match[] = {
    { .compatible = "oem,aboard", },
    {}
};
MODULE_DEVICE_TABLE(of, aboard_of_match);

static struct platform_driver aboard_driver = {
    .driver = {
        .name       = "op_aboard",
        .owner      = THIS_MODULE,
        .of_match_table = aboard_of_match,
    },
    .probe = oem_aboard_probe,
};

static int __init init_project(void)
{
    int ret;

    init_project_info();

    if( GET_PROJECT_INFO(hw_version) > 32 ){
        ret = platform_driver_register(&aboard_driver);
        if (ret)
            pr_err("aboard_driver register failed: %d\n", ret);
    }else {
        ret = 0;
    }
    return ret;
}


subsys_initcall(init_project);
