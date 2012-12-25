/* drivers/video/backlight/lm3537_bl.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <mach/board.h>

#define MAX_LEVEL	    128
#define MIN_LEVEL 		0x00
#define DEFAULT_LEVEL	55

#define I2C_BL_NAME "lm3537"

#define BL_ON	1
#define BL_OFF	0

#ifdef CONFIG_LGE_PM_FACTORY_CURRENT_DOWN
//extern uint16_t battery_info_get(void);
__attribute__((weak)) int usb_cable_info;
#endif

static struct i2c_client *lm3537_i2c_client;

struct backlight_platform_data {
   void (*platform_init)(void);
   int gpio;
   unsigned int mode;
   int max_current;
   int init_on_boot;
   int min_brightness;

   int max_brightness;
 
};

struct lm3537_device {
	struct i2c_client *client;
	struct backlight_device *bl_dev;
	int gpio;
   int max_current;
   int min_brightness;
   int max_brightness;

	struct mutex bl_mutex;
};

static const struct i2c_device_id lm3537_bl_id[] = {
	{ I2C_BL_NAME, 0 },
	{ },
};

static int lm3537_write_reg(struct i2c_client *client, unsigned char reg, unsigned char val);

static int cur_main_lcd_level;
static int saved_main_lcd_level;

static int backlight_status = BL_OFF;
static struct lm3537_device *main_lm3537_dev = NULL;

static void lm3537_hw_reset(void)
{
	int gpio = main_lm3537_dev->gpio;
	
	gpio_tlmm_config(GPIO_CFG(gpio, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
	mdelay(1);
	gpio_set_value(gpio, 1);
	mdelay(1);
}

int lm3537_backlight_status(void)
{
	return backlight_status;
}
EXPORT_SYMBOL(lm3537_backlight_status);

static int lm3537_write_reg(struct i2c_client *client, unsigned char reg, unsigned char val)
{
	int err;
	u8 buf[2];
	struct i2c_msg msg = {	
		client->addr, 0, 2, buf 
	};

	buf[0] = reg;
	buf[1] = val;
	
	if ((err = i2c_transfer(client->adapter, &msg, 1)) < 0) {
		dev_err(&client->dev, "i2c write error\n");
	}
	
	return 0;
}

static void lm3537_set_main_current_level(struct i2c_client *client, int level)
{
	struct lm3537_device *dev;
	int cal_value;
	int min_brightness 		= main_lm3537_dev->min_brightness;
//	int max_brightness 		= main_lm3537_dev->max_brightness;
	
	dev = (struct lm3537_device *)i2c_get_clientdata(client);
	dev->bl_dev->props.brightness = cur_main_lcd_level = level;

	mutex_lock(&main_lm3537_dev->bl_mutex);

	// ACL + default setting
	if(level <23)
		cal_value = (level+(level/5)) + 42;
	else if( (level >=23) && (level<=43) )
		cal_value = level + 47;
	else
		cal_value = (level*4)/10 + 74;

#ifdef CONFIG_LGE_PM_FACTORY_CURRENT_DOWN
	if((usb_cable_info == 6) ||(usb_cable_info == 7)||(usb_cable_info == 11))
	{
		cal_value = min_brightness;
	}
#endif

	if(cal_value > 123)
		cal_value = 123;
	
	lm3537_write_reg(client, 0xA0, cal_value);
//	printk("%s() : cal_value is : 0x%x\n", __func__, cal_value);

	mutex_unlock(&main_lm3537_dev->bl_mutex);
}

void lm3537_init(void)
{
	lm3537_write_reg(main_lm3537_dev->client, 0x00, 0x04);   //group A eanble 
	mdelay(1);
	lm3537_write_reg(main_lm3537_dev->client, 0x10, 0xff);  // device enable with 19mA full scale.
	mdelay(1);
	lm3537_write_reg(main_lm3537_dev->client, 0x20, 0x3f);
	mdelay(1);
	lm3537_write_reg(main_lm3537_dev->client, 0x30, 0x1B);   //speed -0x1B 0x24 0x2D 0x36
}

void lm3537_backlight_on(int level)
{

	if(backlight_status == BL_OFF){
		lm3537_hw_reset();
		lm3537_init();
		backlight_status = BL_ON;
	}
	lm3537_set_main_current_level(main_lm3537_dev->client, level);
	return;
}

void lm3537_backlight_off(void)
{
	int gpio = main_lm3537_dev->gpio;

	saved_main_lcd_level = cur_main_lcd_level;
	lm3537_set_main_current_level(main_lm3537_dev->client, 0);
	lm3537_write_reg(main_lm3537_dev->client, 0x00, 0x00);
	backlight_status = BL_OFF;	

	gpio_tlmm_config(GPIO_CFG(gpio, 0, GPIO_CFG_OUTPUT, GPIO_CFG_NO_PULL, GPIO_CFG_2MA),GPIO_CFG_ENABLE);
	gpio_direction_output(gpio, 0);
	msleep(6);
	return;
}

void lm3537_lcd_backlight_set_level( int level)
{
	if (level > MAX_LEVEL)
		level = MAX_LEVEL;

	if(lm3537_i2c_client!=NULL )
	{		
		if(level == 0) {
			lm3537_backlight_off();
		} else {
			lm3537_backlight_on(level);
		}

		//printk("%s() : level is : %d\n", __func__, level);
	}else{
		printk("%s(): No client\n",__func__);
	}
}
EXPORT_SYMBOL(lm3537_lcd_backlight_set_level);

static int bl_set_intensity(struct backlight_device *bd)
{
	lm3537_lcd_backlight_set_level(bd->props.brightness);
	return 0;
}

static int bl_get_intensity(struct backlight_device *bd)
{
    return cur_main_lcd_level;
}

static ssize_t lcd_backlight_show_level(struct device *dev, struct device_attribute *attr, char *buf)
{
	int r;

	r = snprintf(buf, PAGE_SIZE, "LCD Backlight Level is : %d\n", cur_main_lcd_level);
	
	return r;
}

static ssize_t lcd_backlight_store_level(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int level;

	if (!count)
		return -EINVAL;
	
	level = simple_strtoul(buf, NULL, 10);
	lm3537_lcd_backlight_set_level(level);
	
	return count;
}

static int lm3537_bl_resume(struct i2c_client *client)
{
    lm3537_backlight_on(saved_main_lcd_level);
    
    return 0;
}

static int lm3537_bl_suspend(struct i2c_client *client, pm_message_t state)
{
    printk(KERN_INFO"%s: new state: %d\n",__func__, state.event);

    lm3537_backlight_off();

    return 0;
}

static ssize_t lcd_backlight_show_on_off(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("%s received (prev backlight_status: %s)\n", __func__, backlight_status?"ON":"OFF");
	return 0;
}

static ssize_t lcd_backlight_store_on_off(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int on_off;
	struct i2c_client *client = to_i2c_client(dev); 

	if (!count)
		return -EINVAL;
	
	printk("%s received (prev backlight_status: %s)\n", __func__, backlight_status?"ON":"OFF");
	
	on_off = simple_strtoul(buf, NULL, 10);
	
	printk(KERN_ERR "%d",on_off);
	
	if(on_off == 1){
		lm3537_bl_resume(client);
	}else if(on_off == 0)
		lm3537_bl_suspend(client, PMSG_SUSPEND);
	
	return count;

}
DEVICE_ATTR(lm3537_level, 0644, lcd_backlight_show_level, lcd_backlight_store_level);
DEVICE_ATTR(lm3537_backlight_on_off, 0644, lcd_backlight_show_on_off, lcd_backlight_store_on_off);

static struct backlight_ops lm3537_bl_ops = {
	.update_status = bl_set_intensity,
	.get_brightness = bl_get_intensity,
};

static int lm3537_probe(struct i2c_client *i2c_dev, const struct i2c_device_id *id)
{
	struct backlight_platform_data *pdata;
	struct lm3537_device *dev;
	struct backlight_device *bl_dev;
	struct backlight_properties props;
	int err;

	printk(KERN_INFO"%s: i2c probe start\n", __func__);

	pdata = i2c_dev->dev.platform_data;
	lm3537_i2c_client = i2c_dev;

	dev = kzalloc(sizeof(struct lm3537_device), GFP_KERNEL);
	if(dev == NULL) {
		dev_err(&i2c_dev->dev,"fail alloc for lm3537_device\n");
		return 0;
	}

	main_lm3537_dev = dev;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = MAX_LEVEL;
	
	/* neo.kang@lge.com */
	props.type = BACKLIGHT_PLATFORM;

	bl_dev = backlight_device_register(I2C_BL_NAME, &i2c_dev->dev, NULL, &lm3537_bl_ops, &props);
	bl_dev->props.max_brightness = MAX_LEVEL;
	bl_dev->props.brightness = DEFAULT_LEVEL;
	bl_dev->props.power = FB_BLANK_UNBLANK;
	
	dev->bl_dev = bl_dev;
	dev->client = i2c_dev;
	dev->gpio = pdata->gpio;
	dev->max_current = pdata->max_current;
	dev->min_brightness = pdata->min_brightness;
	dev->max_brightness = pdata->max_brightness;
	i2c_set_clientdata(i2c_dev, dev);

	if(dev->gpio && gpio_request(dev->gpio, "lm3537 reset") != 0) {
		return -ENODEV;
	}
		mutex_init(&dev->bl_mutex);


	err = device_create_file(&i2c_dev->dev, &dev_attr_lm3537_level);
	err = device_create_file(&i2c_dev->dev, &dev_attr_lm3537_backlight_on_off);

	return 0;
}

static int lm3537_remove(struct i2c_client *i2c_dev)
{
	struct lm3537_device *dev;
	int gpio = main_lm3537_dev->gpio;

 	device_remove_file(&i2c_dev->dev, &dev_attr_lm3537_level);
 	device_remove_file(&i2c_dev->dev, &dev_attr_lm3537_backlight_on_off);
	dev = (struct lm3537_device *)i2c_get_clientdata(i2c_dev);
	backlight_device_unregister(dev->bl_dev);
	i2c_set_clientdata(i2c_dev, NULL);

	if (gpio_is_valid(gpio))
		gpio_free(gpio);
	return 0;
}	

static struct i2c_driver main_lm3537_driver = {
	.probe = lm3537_probe,
	.remove = lm3537_remove,
	.suspend = NULL,
	.resume = NULL,
	.id_table = lm3537_bl_id, 
	.driver = {
		.name = I2C_BL_NAME,
		.owner = THIS_MODULE,
	},
};


static int __init lcd_backlight_init(void)
{
	static int err=0;

	err = i2c_add_driver(&main_lm3537_driver);

	return err;
}
 
module_init(lcd_backlight_init);

MODULE_DESCRIPTION("LM3537 Backlight Control");
MODULE_AUTHOR("Jaeseong Gim <jaeseong.gim@lge.com>");
MODULE_LICENSE("GPL");
