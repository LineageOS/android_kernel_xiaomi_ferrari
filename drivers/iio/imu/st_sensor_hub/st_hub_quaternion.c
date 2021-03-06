/*
 * STMicroelectronics st_sensor_hub quaternion driver
 *
 * Copyright 2014 STMicroelectronics Inc.
 * Denis Ciocca <denis.ciocca@st.com>
 * Copyright (C) 2015 XiaoMi, Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <asm/unaligned.h>

#include "st_sensor_hub.h"

#define ST_HUB_QUAT_NUM_DATA_CH		5

static const struct iio_chan_spec st_hub_quat_ch[] = {
	ST_HUB_DEVICE_CHANNEL(IIO_QUATERNION, 0, 1, IIO_MOD_X, IIO_LE, 32, 32,
		BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE), 0, 0, 's'),
	ST_HUB_DEVICE_CHANNEL(IIO_QUATERNION, 1, 1, IIO_MOD_Y, IIO_LE, 32, 32,
		BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE), 0, 0, 's'),
	ST_HUB_DEVICE_CHANNEL(IIO_QUATERNION, 2, 1, IIO_MOD_Z, IIO_LE, 32, 32,
		BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE), 0, 0, 's'),
	ST_HUB_DEVICE_CHANNEL(IIO_QUATERNION, 3, 1, IIO_MOD_MODULE,
		IIO_LE, 32, 32,
		BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE), 0, 0, 's'),
	ST_HUB_DEVICE_CHANNEL(IIO_QUATERNION, 4, 1, IIO_MOD_ACCURACY, IIO_LE,
		8, 8, BIT(IIO_CHAN_INFO_RAW), 0, 0, 'u'),
	IIO_CHAN_SOFT_TIMESTAMP(5)
};

static ST_HUB_DEV_ATTR_SAMP_FREQ_AVAIL();
static ST_HUB_DEV_ATTR_SAMP_FREQ();
static ST_HUB_BATCH_MAX_EVENT_COUNT();
static ST_HUB_BATCH_BUFFER_LENGTH();
static ST_HUB_BATCH_TIMEOUT();
static ST_HUB_BATCH_AVAIL();
static ST_HUB_BATCH();

static void st_hub_quat_push_data(struct platform_device *pdev,
						u8 *data, int64_t timestamp)
{
	int i;
	u8 *sensor_data = data;
	size_t byte_for_channel;
	unsigned int init_copy = 0;
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct st_hub_sensor_data *qdata = iio_priv(indio_dev);

	for (i = 0; i < ST_HUB_QUAT_NUM_DATA_CH; i++) {
		byte_for_channel =
			indio_dev->channels[i].scan_type.storagebits >> 3;
		if (test_bit(i, indio_dev->active_scan_mask)) {
			memcpy(&qdata->buffer[init_copy],
						sensor_data, byte_for_channel);
			init_copy += byte_for_channel;
		}
		sensor_data += byte_for_channel;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, qdata->buffer, timestamp);
}

static int st_hub_read_axis_data(struct iio_dev *indio_dev,
						unsigned int index, int *data)
{
	int err;
	u8 *outdata;
	struct st_hub_sensor_data *adata = iio_priv(indio_dev);
	struct st_hub_pdata_info *info = indio_dev->dev.parent->platform_data;
	unsigned int byte_for_channel =
			indio_dev->channels[0].scan_type.storagebits >> 3;

	outdata = kmalloc(adata->cdata->payload_byte, GFP_KERNEL);
	if (!outdata)
		return -EINVAL;

	err = st_hub_set_enable(info->hdata, info->index, true, true, 0, true);
	if (err < 0)
		goto st_hub_read_axis_free_memory;

	err = st_hub_read_axis_data_asincronous(info->hdata, info->index,
					outdata, adata->cdata->payload_byte);
	if (err < 0)
		goto st_hub_read_axis_free_memory;

	err = st_hub_set_enable(info->hdata, info->index, false, true, 0, true);
	if (err < 0)
		goto st_hub_read_axis_free_memory;

	if (index == (ST_HUB_QUAT_NUM_DATA_CH - 1)) {
		*data = outdata[byte_for_channel * index];
		err = IIO_VAL_INT;
	} else {
		*data = (s32)get_unaligned_le32(
					&outdata[byte_for_channel * index]);
		err = IIO_VAL_FRACTIONAL_LOG2;
	}

st_hub_read_axis_free_memory:
	kfree(outdata);
	return err;
}

static int st_hub_quat_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *ch, int *val, int *val2, long mask)
{
	int err;
	struct st_hub_sensor_data *adata = iio_priv(indio_dev);

	*val = 0;
	*val2 = 0;
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED)
			return -EBUSY;

		err = st_hub_read_axis_data(indio_dev, ch->scan_index, val);
		if (err < 0)
			return err;

		*val = *val >> ch->scan_type.shift;

		return err;
	case IIO_CHAN_INFO_SCALE:
		*val = adata->cdata->gain;
		return IIO_VAL_INT;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static struct attribute *st_hub_quat_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_batch_mode_max_event_count.dev_attr.attr,
	&iio_dev_attr_batch_mode_buffer_length.dev_attr.attr,
	&iio_dev_attr_batch_mode_timeout.dev_attr.attr,
	&iio_dev_attr_batch_mode_available.dev_attr.attr,
	&iio_dev_attr_batch_mode.dev_attr.attr,
	NULL,
};

static const struct attribute_group st_hub_quat_attribute_group = {
	.attrs = st_hub_quat_attributes,
};

static const struct iio_info st_hub_quat_info = {
	.driver_module = THIS_MODULE,
	.attrs = &st_hub_quat_attribute_group,
	.read_raw = &st_hub_quat_read_raw,
};

static const struct iio_buffer_setup_ops st_hub_buffer_setup_ops = {
	.preenable = &st_hub_buffer_preenable,
	.postenable = &st_hub_buffer_postenable,
	.predisable = &st_hub_buffer_predisable,
};

static int st_hub_quat_probe(struct platform_device *pdev)
{
	int err;
	struct iio_dev *indio_dev;
	struct st_hub_pdata_info *info;
	struct st_hub_sensor_data *adata;
	struct st_sensor_hub_callbacks callback;

	indio_dev = iio_device_alloc(sizeof(*adata));
	if (!indio_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->channels = st_hub_quat_ch;
	indio_dev->num_channels = ARRAY_SIZE(st_hub_quat_ch);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->info = &st_hub_quat_info;
	indio_dev->name = pdev->name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	adata = iio_priv(indio_dev);
	info = pdev->dev.platform_data;
	st_hub_get_common_data(info->hdata, info->index, &adata->cdata);

	err = st_hub_set_default_values(adata, info, indio_dev);
	if (err < 0)
		goto st_hub_deallocate_device;

	err = iio_triggered_buffer_setup(indio_dev, NULL,
						NULL, &st_hub_buffer_setup_ops);
	if (err)
		goto st_hub_deallocate_device;

	err = st_hub_setup_trigger_sensor(indio_dev, adata);
	if (err < 0)
		goto st_hub_clear_buffer;

	err = iio_device_register(indio_dev);
	if (err)
		goto st_hub_remove_trigger;

	callback.pdev = pdev;
	callback.push_data = &st_hub_quat_push_data;
	st_hub_register_callback(info->hdata, &callback, info->index);

	return 0;

st_hub_remove_trigger:
	st_hub_remove_trigger(adata);
st_hub_clear_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
st_hub_deallocate_device:
	iio_device_free(indio_dev);
	return err;
}

static int st_hub_quat_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct st_hub_sensor_data *adata = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	st_hub_remove_trigger(adata);
	iio_triggered_buffer_cleanup(indio_dev);
	iio_device_free(indio_dev);

	return 0;
}

static struct platform_device_id st_hub_quat_ids[] = {
	{ CONCATENATE_STRING(LIS331EB_DEV_NAME, "quat") },
	{ CONCATENATE_STRING(LSM6DB0_DEV_NAME, "quat") },
	{ CONCATENATE_STRING(LIS331EB_DEV_NAME, "game_quat") },
	{ CONCATENATE_STRING(LSM6DB0_DEV_NAME, "game_quat") },
	{ CONCATENATE_STRING(LIS331EB_DEV_NAME, "geo_quat") },
	{ CONCATENATE_STRING(LSM6DB0_DEV_NAME, "geo_quat") },
	{ CONCATENATE_STRING(LIS331EB_DEV_NAME, "quat_wk") },
	{ CONCATENATE_STRING(LSM6DB0_DEV_NAME, "quat_wk") },
	{ CONCATENATE_STRING(LIS331EB_DEV_NAME, "game_q_wk") },
	{ CONCATENATE_STRING(LSM6DB0_DEV_NAME, "game_quat_wk") },
	{ CONCATENATE_STRING(LIS331EB_DEV_NAME, "geo_q_wk") },
	{ CONCATENATE_STRING(LSM6DB0_DEV_NAME, "geo_quat_wk") },
	{},
};
MODULE_DEVICE_TABLE(platform, st_hub_quat_ids);

static struct platform_driver st_hub_quat_platform_driver = {
	.id_table = st_hub_quat_ids,
	.driver = {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.probe		= st_hub_quat_probe,
	.remove		= st_hub_quat_remove,
};
module_platform_driver(st_hub_quat_platform_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics sensor-hub quaternions driver");
MODULE_LICENSE("GPL v2");
