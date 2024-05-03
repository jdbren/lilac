#include <lilac/device.h>

int __must_check device_register(struct device *dev)
{
    mutex_init(&dev->mutex);

}
