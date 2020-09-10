/*
 * mikroBUS manifest parsing
 * based on Greybus Manifest Parsing logic
 *
 */
#define pr_fmt(fmt) "mikrobus_manifest: " fmt

#include <linux/bits.h>
#include <linux/types.h>
#include <linux/property.h>

#include "mikrobus_manifest.h"

struct manifest_desc {
    struct list_head links;
    size_t size;
    void* data;
    enum mikrobus_descriptor_type type;
};

static void release_manifest_descriptor(struct manifest_desc* descriptor)
{
    list_del(&descriptor->links);
    kfree(descriptor);
}

static void release_manifest_descriptors(struct click_board_info* info)
{
    struct manifest_desc* descriptor;
    struct manifest_desc* next;

    list_for_each_entry_safe(descriptor, next, &info->manifest_descs, links)
        release_manifest_descriptor(descriptor);
}

static int identify_descriptor(struct click_board_info* info,
    struct mikrobus_descriptor* desc, size_t size)
{
    struct mikrobus_descriptor_header* desc_header = &desc->header;
    struct manifest_desc* descriptor;
    size_t desc_size;
    size_t expected_size;

    if (size < sizeof(*desc_header)) {
        return -EINVAL;
    }

    desc_size = le16_to_cpu(desc_header->size);
    if (desc_size > size) {
        return -EINVAL;
    }

    expected_size = sizeof(*desc_header);

    pr_debug("descriptor type: %d \n", desc_header->type);
    pr_debug("descriptor size: %d \n", desc_size);

    switch (desc_header->type) {

    case MIKROBUS_TYPE_STRING:
        expected_size += sizeof(struct mikrobus_descriptor_string);
        expected_size += desc->string.length;
        pr_debug("string descriptor length : %d \n", desc->string.length);
        pr_debug("string descriptor id : %d \n", desc->string.id);
        expected_size = ALIGN(expected_size, 4);
        break;
    case MIKROBUS_TYPE_PROPERTY:
        expected_size += sizeof(struct mikrobus_descriptor_property);
        expected_size += desc->property.length;
        pr_debug("property descriptor id : %d \n", desc->property.id);
        pr_debug("property descriptor length : %d \n", desc->property.length);
        expected_size = ALIGN(expected_size, 4);
        break;
    case MIKROBUS_TYPE_DEVICE:
        expected_size += sizeof(struct mikrobus_descriptor_device);
        pr_debug("device descriptor num properties : %d \n", desc->device.num_properties);
        break;
    case MIKROBUS_TYPE_INVALID:
    default:
        return -EINVAL;
    }

    descriptor = kzalloc(sizeof(*descriptor), GFP_KERNEL);
    if (!descriptor)
        return -ENOMEM;

    descriptor->size = desc_size;
    descriptor->data = (char*)desc + sizeof(*desc_header);
    descriptor->type = desc_header->type;
    list_add_tail(&descriptor->links, &info->manifest_descs);

    return desc_size;
}

static char* mikrobus_string_get(struct click_board_info* info, u8 string_id)
{
    struct mikrobus_descriptor_string* desc_string;
    struct manifest_desc* descriptor;
    bool found = false;
    char* string;

    if (!string_id)
        return NULL;

    if (descriptor->type == MIKROBUS_TYPE_STRING) {
        list_for_each_entry(descriptor, &info->manifest_descs, links)
        {
            desc_string = descriptor->data;
            if (desc_string->id == string_id) {
                found = true;
                break;
            }
        }
    }
    if (!found)
        return ERR_PTR(-ENOENT);

    string = kmemdup(&desc_string->string, desc_string->length + 1,
        GFP_KERNEL);
    if (!string)
        return ERR_PTR(-ENOMEM);
    string[desc_string->length] = '\0';

    release_manifest_descriptor(descriptor);

    return string;
}

static struct property_entry* mikrobus_property_entry_get(struct click_board_info* info, u8* prop_link, int num_properties)
{
    struct mikrobus_descriptor_property* desc_property;
    struct manifest_desc* descriptor;
    struct property_entry* properties;
    int i;
    char* prop_name;
    bool found = false;
    u8* val_u8;
    u16* val_u16;
    u32* val_u32;
    u64* val_u64;

    properties = kzalloc(sizeof(*properties) * num_properties, GFP_KERNEL);
    if (!properties)
        return ERR_PTR(-ENOMEM);

    for (i = 0; i < num_properties; i++) {

        if (descriptor->type == MIKROBUS_TYPE_PROPERTY) {
            list_for_each_entry(descriptor, &info->manifest_descs, links)
            {
                desc_property = descriptor->data;
                if (desc_property->id == prop_link[i]) {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            return ERR_PTR(-ENOENT);

        prop_name = mikrobus_string_get(info, desc_property->propname_stringid);

        switch (desc_property->type) {

        case MIKROBUS_PROPERTY_TYPE_U8:
            val_u8 = kmemdup(&desc_property->value, (desc_property->length) * sizeof(u8), GFP_KERNEL);
            if (desc_property->length == 1)
                properties[i] = PROPERTY_ENTRY_U8(prop_name, *val_u8);
            else
                properties[i] = PROPERTY_ENTRY_U8_ARRAY_LEN(prop_name, (void*)desc_property->value, desc_property->length);
            break;
        case MIKROBUS_PROPERTY_TYPE_U16:
            val_u16 = kmemdup(&desc_property->value, (desc_property->length) * sizeof(u16), GFP_KERNEL);
            if (desc_property->length == 1)
                properties[i] = PROPERTY_ENTRY_U16(prop_name, *val_u16);
            else
                properties[i] = PROPERTY_ENTRY_U16_ARRAY_LEN(prop_name, (void*)desc_property->value, desc_property->length);
            break;
        case MIKROBUS_PROPERTY_TYPE_U32:
            val_u32 = kmemdup(&desc_property->value, (desc_property->length) * sizeof(u32), GFP_KERNEL);
            if (desc_property->length == 1)
                properties[i] = PROPERTY_ENTRY_U32(prop_name, *val_u32);
            else
                properties[i] = PROPERTY_ENTRY_U32_ARRAY_LEN(prop_name, (void*)desc_property->value, desc_property->length);
            break;
        case MIKROBUS_PROPERTY_TYPE_U64:
            val_u64 = kmemdup(&desc_property->value, (desc_property->length) * sizeof(u64), GFP_KERNEL);
            if (desc_property->length == 1)
                properties[i] = PROPERTY_ENTRY_U64(prop_name, *val_u64);
            else
                properties[i] = PROPERTY_ENTRY_U64_ARRAY_LEN(prop_name, (void*)desc_property->value, desc_property->length);
            break;
        default:
            return ERR_PTR(-EINVAL);
        }

        release_manifest_descriptor(descriptor);

        pr_debug("Property Name %s \n", properties[i].name);
        pr_debug("Property type %d \n", properties[i].type);
    }

    return properties;
}

static u8* mikrobus_property_link_get(struct click_board_info* info, u8 prop_id, u8 prop_type)
{
    struct mikrobus_descriptor_property* desc_property;
    struct manifest_desc* descriptor;
    bool found = false;
    u8* val_u8;

    if (!prop_id)
        return NULL;

    if (descriptor->type == MIKROBUS_TYPE_PROPERTY) {
        list_for_each_entry(descriptor, &info->manifest_descs, links)
        {
            desc_property = descriptor->data;
            if (desc_property->id == prop_id && desc_property->type == prop_type) {
                found = true;
                break;
            }
        }
    }
    if (!found)
        return ERR_PTR(-ENOENT);

    val_u8 = kmemdup(&desc_property->value, desc_property->length, GFP_KERNEL);
    release_manifest_descriptor(descriptor);

    return val_u8;
}

static int mikrobus_manifest_attach_device(struct click_board_info* info,
    struct mikrobus_descriptor_device* dev_desc)
{
    struct click_device_info* dev;
    struct gpiod_lookup_table* lookup;
    struct mikrobus_descriptor_property* desc_property;
    struct manifest_desc* descriptor;
    int i;
    u8* prop_link;
    u8* gpio_desc_link;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        return -ENOMEM;
    }

    dev->id = dev_desc->id;
    dev->drv_name = mikrobus_string_get(info, dev_desc->driver_stringid);
    dev->protocol = dev_desc->protocol;
    dev->reg = dev_desc->reg;
    dev->irq = dev_desc->irq;
    dev->irq_type = dev_desc->irq_type;
    dev->max_speed_hz = le32_to_cpu(dev_desc->max_speed_hz);
    dev->mode = dev_desc->mode;
    dev->cs_gpio = dev_desc->cs_gpio;
    dev->num_gpio_resources = dev_desc->num_gpio_resources;
    dev->num_properties = dev_desc->num_properties;

    pr_info("Device %d , number of properties=%d \n", dev->id, dev->num_properties);

    if (dev->num_properties > 0) {
        prop_link = mikrobus_property_link_get(info, dev_desc->prop_link, MIKROBUS_PROPERTY_TYPE_LINK);
        dev->properties = mikrobus_property_entry_get(info, prop_link, dev->num_properties);
    }

    if (dev->num_gpio_resources > 0) {

        lookup = kzalloc(struct_size(lookup, table, dev->num_gpio_resources), GFP_KERNEL);
        if (!lookup)
            return -ENOMEM;

        gpio_desc_link = mikrobus_property_link_get(info, dev_desc->gpio_link, MIKROBUS_PROPERTY_TYPE_GPIO);
        for (i = 0; i < dev->num_gpio_resources; i++) {

            if (descriptor->type == MIKROBUS_TYPE_PROPERTY) {
                list_for_each_entry(descriptor, &info->manifest_descs, links)
                {
                    desc_property = descriptor->data;
                    if (desc_property->id == gpio_desc_link[i]) {
                        lookup->table[i].chip_hwnum = *desc_property->value;
                        lookup->table[i].con_id = mikrobus_string_get(info, desc_property->propname_stringid);
                        break;
                    }
                }
            }
        }
        dev->gpio_lookup = lookup;
    }

    list_add_tail(&dev->links, &info->devices);

    return 0;
}

static int mikrobus_manifest_parse_devices(struct click_board_info* info)
{
    struct mikrobus_descriptor_device* desc_device;
    struct manifest_desc *desc, *next;
    int devcount = 0;

    if (WARN_ON(!list_empty(&info->devices)))
        return false;

    if (desc->type != MIKROBUS_TYPE_DEVICE) {
        list_for_each_entry_safe(desc, next, &info->manifest_descs, links)
        {
            desc_device = desc->data;
            pr_debug(" Click Device ID : %d \n", desc_device->id);
            pr_debug(" Click Device protocol : %d \n", desc_device->protocol);
            pr_debug(" Click Device reg : %d \n", desc_device->reg);
            pr_debug(" Click Device max_speed_hz : %d \n", desc_device->max_speed_hz);
            pr_debug(" Click Device mode : %d \n", desc_device->mode);
            pr_debug(" Click Device irq : %d \n", desc_device->irq);
            pr_debug(" Click Device irq_type : %d \n", desc_device->irq_type);
            pr_debug(" Click Device cs_gpio : %d \n", desc_device->cs_gpio);
            pr_debug(" Click Device num_gpio_resources : %d \n", desc_device->num_gpio_resources);
            pr_debug(" Click Device nume_properties : %d \n", desc_device->num_properties);
            mikrobus_manifest_attach_device(info, desc_device);
            devcount++;
        }
    }

    return devcount;
}

bool mikrobus_manifest_parse(struct click_board_info* info, void* data, size_t size)
{
    struct mikrobus_manifest* manifest;
    struct mikrobus_manifest_header* header;
    struct mikrobus_descriptor* desc;
    u16 manifest_size;
    bool result;
    int dev_count;

    if (WARN_ON(!list_empty(&info->manifest_descs)))
        return false;

    if (size < sizeof(*header))
        return false;

    manifest = data;
    header = &manifest->header;
    manifest_size = le16_to_cpu(header->size);

    if (manifest_size != size)
        return false;

    if (header->version_major > MIKROBUS_VERSION_MAJOR) {
        return false;
    }

    desc = manifest->descriptors;
    size -= sizeof(*header);
    while (size) {
        int desc_size;

        desc_size = identify_descriptor(info, desc, size);
        if (desc_size < 0) {
            result = false;
        }
        desc = (struct mikrobus_descriptor*)((char*)desc + desc_size);
        size -= desc_size;
    }

    info->name = mikrobus_string_get(info, header->click_stringid);
    info->num_devices = header->num_devices;
    info->rst_gpio_state = header->rst_gpio_state;
    info->pwm_gpio_state = header->pwm_gpio_state;
    info->int_gpio_state = header->int_gpio_state;

    pr_debug(" Click Board Name : %s \n", info->name);
    pr_debug(" Click Board Num Devices : %d \n", info->num_devices);
    pr_debug(" Click Board RST GPIO State : %d \n", info->rst_gpio_state);
    pr_debug(" Click Board PWM GPIO State : %d \n", info->pwm_gpio_state);
    pr_debug(" Click Board INT GPIO State : %d \n", info->int_gpio_state);

    dev_count = mikrobus_manifest_parse_devices(info);

    pr_info(" %s click manifest parsed with %d device(s) \n", info->name, info->num_devices);

    release_manifest_descriptors(info);

    return true;
}
EXPORT_SYMBOL_GPL(mikrobus_manifest_parse);

size_t mikrobus_manifest_header_validate(void* data, size_t size)
{
    struct mikrobus_manifest_header* header;
    u16 manifest_size;

    pr_info("manifest header validate size %d , header size is %d \n", size, sizeof(*header));

    if (size < sizeof(*header))
        return 0;

    header = data;
    manifest_size = le16_to_cpu(header->size);

    if (header->version_major > MIKROBUS_VERSION_MAJOR) {
        return 0;
    }
    if ((header->click_stringid < 1) || (header->num_devices < 1)) {
        return 0;
    }

    return manifest_size;
}
EXPORT_SYMBOL_GPL(mikrobus_manifest_header_validate);
