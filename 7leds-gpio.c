#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/init.h>


#define MAX_LEDS_COUNT (56)
#define LEDS_PER_7SEG  (7)
#define LED_OFF 0
#define LED_ON 1

unsigned int seg[]={	0X3F, //Hex value to display the number 0
                    	0X06, //Hex value to display the number 1
                    	0X5B, //Hex value to display the number 2
                    	0X4F, //Hex value to display the number 3
                    	0X66, //Hex value to display the number 4
                    	0X6D, //Hex value to display the number 5
                    	0X7C, //Hex value to display the number 6
                    	0X07, //Hex value to display the number 7
                    	0X7F, //Hex value to display the number 8
                    	0X6F, //Hex value to display the number 9
			0X77, //A
			0X1F, //B
			0X4E, //C
			0X3D, //D
			0X4F, //E
			0X47, //F
                   }; //End of Array for displaying numbers from 0 to 9

struct gpio_led_data {
	struct led_classdev cdev;
	struct gpio_desc *gpiod;
	u8 can_sleep;
};

struct seven_leds_attr {
        struct attribute attr;
	int leds_count;
	int num_count;
        struct gpio_led_data *led_dat[MAX_LEDS_COUNT];
};

static struct seven_leds_attr _seven_leds_attr = {
        .attr.name="value",
        .attr.mode = 0644,
	.leds_count = 0,
};

static struct attribute * seven_segs_attr[] = {
        &_seven_leds_attr.attr,
        NULL
};

static long pow(int a, int b)
{
	long temp = 1;
	while(b > 0)
	{
		temp *= a;
		b--;
	}
	return temp;
}

static inline struct gpio_led_data *
			cdev_to_gpio_led_data(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct gpio_led_data, cdev);
}

static void gpio_led_get(struct gpio_led_data *led_dat,
        int *value)
{
        if (led_dat->can_sleep)
                *value = gpiod_get_value_cansleep(led_dat->gpiod);
        else
                *value = gpiod_get_value(led_dat->gpiod);
}

static void gpio_led_set(struct gpio_led_data *led_dat,
	int value)
{
	if (led_dat->can_sleep)
		gpiod_set_value_cansleep(led_dat->gpiod, value);
	else
		gpiod_set_value(led_dat->gpiod, value);
}

static ssize_t default_show(struct kobject *kobj, struct attribute *attr,
        char *buf)
{
	int value = 0;
	int num = 0, tmp_num;
	int i,j;
	char show_value[MAX_LEDS_COUNT/LEDS_PER_7SEG];
	
	struct seven_leds_attr *a = container_of(attr, struct seven_leds_attr, attr);

	for(i = 0; i < a->num_count ; i++)
	{
		tmp_num = 0;
		for(j = 0; j < 7; j++)
		{
			gpio_led_get(a->led_dat[i*7 + j], &value);
			tmp_num |= value << j;
		}
		for(j=0; j<10; j++)
		{
			if(tmp_num == seg[j])break;
		}
		if(j == 10)
		{
			show_value[i] = 0x5F; // under socre
		}
		else
		{
			show_value[i] = j + 0x30;
		}
		//num += j*pow(10, a->num_count -1 - i); 
	}

	show_value[i] = '\0';
	//return scnprintf(buf, PAGE_SIZE, "%d\n", num);;
	return scnprintf(buf, PAGE_SIZE, "%s\n", show_value);;
}

static ssize_t default_store(struct kobject *kobj, struct attribute *attr,
        const char *buf, size_t len)
{
	struct seven_leds_attr *a = container_of(attr, struct seven_leds_attr, attr);
	int i=0, j=0;
	int value = 0;
	int ret = 0;
	int num = 0;
	ret = sscanf(buf, "%d", &value);
	
	if(ret > 0 && value < pow(10, a->num_count))
	{
		printk("Write to 7-segs value: %d\n", value);
		//reset all segs
		for(j=0; j < a->leds_count; j++)
		{
			ret = gpiod_direction_output(a->led_dat[j]->gpiod, LED_OFF);
			if(ret < 0)return -1;
		}
		//set number for each segs
		for(j=0; j < (len > a->num_count ? a->num_count : (buf[len-1] == '\n' ?(len-1):len)); j++)
		{
			//num = value % (10*j);
			num = buf[j] - '0';
			for(i = 0; i < 7; i++)
			{
				if(((seg[num] >> i) & 0x1) == 0x1)
				{
					gpio_led_set(a->led_dat[((buf[len-1] == '\n' ? (a->num_count + 1 - len):(a->num_count - len)) + j)*7 + i], LED_ON);
					if(ret < 0)return -1;
				}
			}
		}
	}
	else if ((0x20 == buf[0]) &&  (0x20 == buf[1]) && (0x20 == buf[2]))
	{
		for(j=0; j < a->leds_count; j++)
		{
			ret = gpiod_direction_output(a->led_dat[j]->gpiod, LED_OFF);
			if(ret < 0)return -1;
		}
	}

	return len;
}
static int create_gpio_led(const struct gpio_led *template,
	struct gpio_led_data *led_dat, struct device *parent)
{
	int ret, state;
	led_dat->gpiod = template->gpiod;
	if (!led_dat->gpiod) {
		/*
		 * This is the legacy code path for platform code that
		 * still uses GPIO numbers. Ultimately we would like to get
		 * rid of this block completely.
		 */
		unsigned long flags = GPIOF_OUT_INIT_LOW;

		/* skip leds that aren't available */
		if (!gpio_is_valid(template->gpio)) {
			dev_info(parent, "Skipping unavailable LED gpio %d (%s)\n",
					template->gpio, template->name);
			return 0;
		}

		if (template->active_low)
			flags |= GPIOF_ACTIVE_LOW;

		ret = devm_gpio_request_one(parent, template->gpio, flags,
					    template->name);
		if (ret < 0)
			return ret;

		led_dat->gpiod = gpio_to_desc(template->gpio);
		if (!led_dat->gpiod)
			return -EINVAL;
	}
	led_dat->can_sleep = gpiod_cansleep(led_dat->gpiod);
	state = (template->default_state == LEDS_GPIO_DEFSTATE_ON);

	ret = gpiod_direction_output(led_dat->gpiod, state);
	if (ret < 0)
		return ret;

	return 0;
}

struct gpio_leds_priv {
	int num_leds;
	struct gpio_led_data leds[];
};

static inline int sizeof_gpio_leds_priv(int num_leds)
{
	return sizeof(struct gpio_leds_priv) +
		(sizeof(struct gpio_led_data) * num_leds);
}

static struct gpio_leds_priv *gpio_leds_create(struct seven_leds_attr *_seven_leds_attr, struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *child;
	struct gpio_leds_priv *priv;
	int count, ret, i = 0;
	
	count = device_get_child_node_count(dev);
	if (!count)
		return ERR_PTR(-ENODEV);

	priv = devm_kzalloc(dev, sizeof_gpio_leds_priv(count), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	device_for_each_child_node(dev, child) {
		_seven_leds_attr->led_dat[i] = &priv->leds[priv->num_leds];
		struct gpio_led led = {};
		const char *state = NULL;
		struct device_node *np = to_of_node(child);

		led.gpiod = devm_get_gpiod_from_child(dev, NULL, child, GPIOD_ASIS);
		if (IS_ERR(led.gpiod)) {
			fwnode_handle_put(child);
			return ERR_CAST(led.gpiod);
		}

		ret = fwnode_property_read_string(child, "label", &led.name);
		if (ret && IS_ENABLED(CONFIG_OF) && np)
			led.name = np->name;
		if (!led.name) {
			fwnode_handle_put(child);
			return ERR_PTR(-EINVAL);
		}

		ret = create_gpio_led(&led, _seven_leds_attr->led_dat[i], dev);
		if (ret < 0) {
			fwnode_handle_put(child);
			return ERR_PTR(ret);
		}

		priv->num_leds++;
		i++;
	}
	_seven_leds_attr->leds_count = i;
	_seven_leds_attr->num_count = 3;
	return priv;
}

static const struct of_device_id of_gpio_leds_match[] = {
	{ .compatible = "fp_7segs", },
	{},
};

MODULE_DEVICE_TABLE(of, of_gpio_leds_match);

static struct sysfs_ops seven_segs_ops = {
    .show = default_show,
    .store = default_store,
};

static struct kobj_type seven_segs_type = {
    .sysfs_ops = &seven_segs_ops,
    .default_attrs = seven_segs_attr,
};

struct kobject *seven_segs_kobj;

static int gpio_7leds_probe(struct platform_device *pdev)
{
	struct gpio_led_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct gpio_leds_priv *priv;
	int i, ret = 0, err;
	
	if (pdata && pdata->num_leds) {
		priv = devm_kzalloc(&pdev->dev,
				sizeof_gpio_leds_priv(pdata->num_leds),
					GFP_KERNEL);
		if (!priv)
			return -ENOMEM;

		priv->num_leds = pdata->num_leds;
		for (i = 0; i < priv->num_leds; i++) {
			ret = create_gpio_led(&pdata->leds[i],
					      &priv->leds[i],
					      &pdev->dev);
			if (ret < 0)
				return ret;
		}
	} else {
		priv = gpio_leds_create(&_seven_leds_attr, pdev);
		if (IS_ERR(priv))
			return PTR_ERR(priv);
	}
	
	seven_segs_kobj = kzalloc(sizeof(*seven_segs_kobj), GFP_KERNEL);
    	if (seven_segs_kobj) {
		kobject_init(seven_segs_kobj, &seven_segs_type);
		if (kobject_add(seven_segs_kobj, NULL, "%s", "fp_7segs")) {
			err = -1;
			printk("Sysfs seven_segs_kobj creation failed\n");
			kobject_put(seven_segs_kobj);
			seven_segs_kobj = NULL;
		}
		err = 0;
	}
	
	platform_set_drvdata(pdev, priv);

	return 0;
}

static int gpio_7leds_remove(struct platform_device *pdev)
{
	if (seven_segs_kobj) {
        	kobject_put(seven_segs_kobj);
        	kfree(seven_segs_kobj);
    	}
	return 0;
}

static struct platform_driver gpio_7leds_driver = {
	.probe		= gpio_7leds_probe,
	.remove		= gpio_7leds_remove,
	.driver		= {
		.name	= "seven-leds-gpio",
		.of_match_table = of_gpio_leds_match,
	},
};

static int __init sysfsexample_module_init(void)
{
	int err;
	seven_segs_kobj = kzalloc(sizeof(*seven_segs_kobj), GFP_KERNEL);
        if (seven_segs_kobj) {
                kobject_init(seven_segs_kobj, &seven_segs_type);
                if (kobject_add(seven_segs_kobj, NULL, "%s", "fp_7segs")) {
                        err = -1;
                        printk("Sysfs seven_segs_kobj creation failed\n");
                        kobject_put(seven_segs_kobj);
                        seven_segs_kobj = NULL;
                }
                err = 0;
        }
	return 0;
}

static void __exit sysfsexample_module_exit(void)
{
    if (seven_segs_kobj) {
        kobject_put(seven_segs_kobj);
        kfree(seven_segs_kobj);
    }
}

module_platform_driver(gpio_7leds_driver);

MODULE_AUTHOR("Willie");
MODULE_DESCRIPTION("7-LEDs GPIO driver");
MODULE_LICENSE("GPL");
