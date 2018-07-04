/*For OEM project monitor RF cable connection status,
 * and config different RF configuration
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
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <soc/qcom/subsystem_restart.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/op_rf_cable_monitor.h>

static struct project_info *project_info_desc;

struct cable_data {
	int irq_0;
	int irq_1;
	int cable_gpio_0;//gpio32
	int cable_gpio_1;//gpio86
	struct delayed_work work;
	struct workqueue_struct *wqueue;
	struct device *dev;
	struct wakeup_source wl;
	atomic_t running;
	int rf_v3;
	int rf_v3_pre;
	spinlock_t lock;
	int enable;
};
static struct cable_data *_cdata;
static DEFINE_MUTEX(sem);

static char *cmdline_find_option(char *str)
{
	return strnstr(saved_command_line, str, strlen(saved_command_line));
}

int modify_rf_cable_smem_info(uint32 status)
{
	project_info_desc = smem_find(SMEM_PROJECT_INFO,
		sizeof(struct project_info), 0,
		SMEM_ANY_HOST_FLAG);

	if (IS_ERR_OR_NULL(project_info_desc))
		pr_err("%s: get project_info failure\n", __func__);
	else {
		project_info_desc->rf_v3 = status;
		pr_err("%s: rf_cable: %d\n",
			__func__, project_info_desc->rf_v3);
	}
	return 0;
}


static void rf_cable_work(struct work_struct *work)
{
	unsigned long flags;

	spin_lock_irqsave(&_cdata->lock, flags);
	disable_irq_nosync(_cdata->irq_0);
	disable_irq_nosync(_cdata->irq_1);
	spin_unlock_irqrestore(&_cdata->lock, flags);
	pr_err("modem :kevin debug:%s, %d:_cdata->rf_v3_pre=%d, _cdata->rf_v3=%d,\n",
		__func__, __LINE__, _cdata->rf_v3_pre, _cdata->rf_v3);

	_cdata->rf_v3 =
		gpio_get_value(_cdata->cable_gpio_0) ||
		gpio_get_value(_cdata->cable_gpio_1);

	modify_rf_cable_smem_info(_cdata->rf_v3);
	if (_cdata->rf_v3 != _cdata->rf_v3_pre)
		op_restart_modem();

	_cdata->rf_v3_pre =
	gpio_get_value(_cdata->cable_gpio_0) ||
	gpio_get_value(_cdata->cable_gpio_1);


	spin_lock_irqsave(&_cdata->lock, flags);
	enable_irq(_cdata->irq_0);
	enable_irq(_cdata->irq_1);
	spin_unlock_irqrestore(&_cdata->lock, flags);
}

irqreturn_t cable_interrupt(int irq, void *_dev)
{
	__pm_wakeup_event(&_cdata->wl,
		msecs_to_jiffies(CABLE_WAKELOCK_HOLD_TIME));
	queue_delayed_work(_cdata->wqueue,
		&_cdata->work, msecs_to_jiffies(1));
	return IRQ_HANDLED;
}

static ssize_t rf_cable_proc_read_func(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	char page[PAGESIZE];
	int len;

	len = scnprintf(page, sizeof(page), "%d\n", _cdata->enable);

	return simple_read_from_buffer(user_buf,
		count, ppos, page, len);
}

static ssize_t rf_cable_proc_write_func(struct file *file,
	const char __user *buffer, size_t count, loff_t *ppos)
{
	unsigned long flags;
	int enable = 0;
	char buf[10];
	int ret;

	if (copy_from_user(buf, buffer, count))  {
		pr_err("%s: read proc input error.\n", __func__);
		return count;
	}

	ret = kstrtoint(buf, 0, &enable);
	if (ret < 0)
		return ret;


	if (enable != _cdata->enable) {
		_cdata->enable = enable;
		if (!_cdata->enable) {
			spin_lock_irqsave(&_cdata->lock, flags);
			disable_irq_nosync(_cdata->irq_0);
			disable_irq_nosync(_cdata->irq_1);
			spin_unlock_irqrestore(&_cdata->lock, flags);
			_cdata->rf_v3 = 1;

			modify_rf_cable_smem_info(1);
			if (!_cdata->rf_v3_pre)
				op_restart_modem();
			_cdata->rf_v3_pre = 1;
		} else {
			spin_lock_irqsave(&_cdata->lock, flags);
			enable_irq(_cdata->irq_0);
			enable_irq(_cdata->irq_1);
			spin_unlock_irqrestore(&_cdata->lock, flags);

			_cdata->rf_v3 = gpio_get_value(_cdata->cable_gpio_0) ||
				gpio_get_value(_cdata->cable_gpio_1);

			modify_rf_cable_smem_info(_cdata->rf_v3);
			if (_cdata->rf_v3 != _cdata->rf_v3_pre)
				op_restart_modem();
			_cdata->rf_v3_pre =
			gpio_get_value(_cdata->cable_gpio_0) ||
			gpio_get_value(_cdata->cable_gpio_1);
		}
	}
	return count;
}

static const struct file_operations rf_enable_proc_fops = {
	.write = rf_cable_proc_write_func,
	.read =  rf_cable_proc_read_func,
	.open = simple_open,
	.owner = THIS_MODULE,
};

int create_rf_cable_procfs(void)
{
	int ret = 0;

	if (!proc_create("rf_cable_config",
		0644, NULL, &rf_enable_proc_fops)) {
		pr_err("%s: proc_create enable fail!\n", __func__);
		ret = -1;
	}
	_cdata->enable = 1;
	return ret;
}

static int op_rf_request_named_gpio(const char *label, int *gpio)
{
	struct device *dev = _cdata->dev;
	struct device_node *np = dev->of_node;
	int rc = of_get_named_gpio(np, label, 0);

	if (rc < 0) {
		dev_err(dev, "failed to get '%s'\n", label);
		*gpio = rc;
		return rc;
	}
	*gpio = rc;
	rc = devm_gpio_request(dev, *gpio, label);
	if (rc) {
		dev_err(dev, "failed to request gpio %d\n", *gpio);
		return rc;
	}
	dev_info(dev, "%s - gpio: %d\n", label, *gpio);
	return 0;
}

static int op_rf_cable_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct device *dev = &pdev->dev;

	if (cmdline_find_option("ftm_mode")) {
		pr_err("%s: ftm_mode FOUND! use 1 always\n", __func__);
		modify_rf_cable_smem_info(1);
	} else {
		_cdata = kzalloc(sizeof(struct cable_data), GFP_KERNEL);
		if (!_cdata) {
			pr_err("%s: failed to allocate memory\n", __func__);
			rc = -ENOMEM;
			goto exit;
		}

		_cdata->dev = dev;
		dev_set_drvdata(dev, _cdata);
//request gpio 0 .gpio 1.
		rc = op_rf_request_named_gpio("rf,cable-gpio-0",
			&_cdata->cable_gpio_0);
		if (rc) {
			pr_err("%s: op_rf_request_named_gpio gpio-0 fail\n",
				__func__);
			goto exit_gpio;
		}
		rc = op_rf_request_named_gpio("rf,cable-gpio-1",
			&_cdata->cable_gpio_1);
		if (rc) {
			pr_err("%s: op_rf_request_named_gpio gpio-1 fail\n",
				__func__);
			goto exit_gpio;
		}
//set input  and gpio to irq.
		gpio_direction_input(_cdata->cable_gpio_0);
		_cdata->irq_0 = gpio_to_irq(_cdata->cable_gpio_0);
		if (_cdata->irq_0 < 0) {
			pr_err("Unable to get irq number for GPIO %d, error %d\n",
				_cdata->cable_gpio_0, _cdata->irq_0);
			rc = _cdata->irq_0;
			goto exit_gpio;
		}

		gpio_direction_input(_cdata->cable_gpio_1);
		_cdata->irq_1 = gpio_to_irq(_cdata->cable_gpio_1);
		if (_cdata->irq_1 < 0) {
			pr_err("Unable to get irq number for GPIO %d, error %d\n",
				_cdata->cable_gpio_1, _cdata->irq_1);
			rc = _cdata->irq_1;
			goto exit_gpio;
		}
//creat workqueue.
		_cdata->wqueue = create_singlethread_workqueue(
			"op_rf_cable_wqueue");
		INIT_DELAYED_WORK(&_cdata->work, rf_cable_work);

//request _irq0
		rc = request_irq(_cdata->irq_0, cable_interrupt,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"op_rf_cable", _cdata);
		if (rc) {
			pr_err("could not request irq %d\n", _cdata->irq_0);
			goto exit_gpio;
		}
		pr_err("requested irq %d\n", _cdata->irq_0);
		enable_irq_wake(_cdata->irq_0);

//request _irq1
		rc = request_irq(_cdata->irq_1, cable_interrupt,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"op_rf_cable", _cdata);
		if (rc) {
			pr_err("could not request irq %d\n", _cdata->irq_1);
			goto exit_gpio;
		}

		pr_err("requested irq %d\n", _cdata->irq_1);
		enable_irq_wake(_cdata->irq_1);

		wakeup_source_init(&_cdata->wl,
			"rf_cable_wake_lock");
		spin_lock_init(&_cdata->lock);
		atomic_set(&_cdata->running,
			gpio_get_value(_cdata->cable_gpio_0) ||
			gpio_get_value(_cdata->cable_gpio_1));

		modify_rf_cable_smem_info(
			gpio_get_value(_cdata->cable_gpio_0) ||
			gpio_get_value(_cdata->cable_gpio_1));
		create_rf_cable_procfs();
	}
	pr_err("%s: probe ok!\n", __func__);
	return 0;

exit_gpio:
	kfree(_cdata);
exit:
	pr_err("%s: probe Fail!\n", __func__);

	return rc;
}

static const struct of_device_id rf_of_match[] = {
	{ .compatible = "oem,rf_cable", },
	{}
};
MODULE_DEVICE_TABLE(of, rf_of_match);

static struct platform_driver op_rf_cable_driver = {
	.driver = {
		.name		= "op_rf_cable",
		.owner		= THIS_MODULE,
		.of_match_table = rf_of_match,
	},
	.probe = op_rf_cable_probe,
};

static int __init op_rf_cable_init(void)
{
	int ret;

	ret = platform_driver_register(&op_rf_cable_driver);
	if (ret)
		pr_err("rf_cable_driver register failed: %d\n", ret);

	return ret;
}

MODULE_LICENSE("GPL v2");
subsys_initcall(op_rf_cable_init);
