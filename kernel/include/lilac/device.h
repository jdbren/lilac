#ifndef _KERNEL_DEVICE_H
#define _KERNEL_DEVICE_H

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/sync.h>
#include <lilac/kmalloc.h>

struct device;
struct device_driver;
struct bus_type;
struct device_type;

#define NULL_DEVICE 0
#define MEM_DEVICE 1
#define TTY_DEVICE 2
#define SATA_DEVICE 3
#define NVME_DEVICE 4

#define DEVICE_ID(maj, min) ((maj << 20) + min)
#define MAJOR(dev) ((dev) >> 20)
#define MINOR(dev) ((dev) & 0xFFFFF)

struct bus_type {
	const char *name;
	int (*match)(struct device *dev, struct device_driver *drv);
	int (*probe)(struct device *dev);
};

#define BUS_TYPE(bus, _match, _probe) \
    struct bus_type bus = { \
        .name = bus, \
        .match = _match, \
        .probe = _probe \
    }

struct device_driver {
	const char *name;
	const struct bus_type *bus;
	int (*probe)(struct device *dev);
	struct driver_private *p;
};

#define DEVICE_DRIVER(driver, _bus, _probe) \
    struct device_driver driver = { \
        .name = #driver, \
        .bus = _bus, \
        .probe = _probe \
    }

struct device_type {
    const char *name;
};

struct device {
    dev_t devnum;
    struct device *parent;
    struct device_private *p;
    const char *init_name;
    const struct device_type *type;
    const struct bus_type *bus;
    struct device_driver *driver;
    void *platform_data;
    void *driver_data;
    struct mutex mutex;
    struct list_head children;
};

#define DEVICE(maj, min, _parent, name, _type, _bus, _driver, pdata, ddata) \
    struct device dev = { \
        .devnum = (maj << 20) + (min), \
        .parent = _parent, \
        .init_name = name, \
        .type = _type, \
        .bus = _bus, \
        .driver = _driver, \
        .platform_data = pdata, \
        .driver_data = ddata, \
        .mutex = MUTEX_INIT \
    }

[[nodiscard]] int device_register(struct device *dev);
[[nodiscard]] int device_unregister(struct device *dev);

static inline struct device *device_alloc(void)
{
    return kzmalloc(sizeof(struct device));
}

struct file_operations;
int add_device(const char *, struct file_operations *);


#endif
