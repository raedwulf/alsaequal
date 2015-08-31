/*
 * Copyright (c) 2008 Cooper Street Innovations
 * 		<charles@cooper-street.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <stdio.h>
#include <alsa/asoundlib.h>
#include <alsa/control_external.h>

#include "ladspa.h"
#include "ladspa_utils.h"

typedef struct snd_ctl_equal_control {
	long min;
	long max;
	char *name;
} snd_ctl_equal_control_t;

typedef struct snd_ctl_equal {
	snd_ctl_ext_t ext;
	void *library;
	const LADSPA_Descriptor *klass;
	int num_input_controls;
	LADSPA_Control *control_data;
	snd_ctl_equal_control_t *control_info;
} snd_ctl_equal_t;

static void equal_close(snd_ctl_ext_t *ext)
{
	snd_ctl_equal_t *equal = ext->private_data;
	int i;
	for (i = 0; i < equal->num_input_controls; i++) {
		free(equal->control_info[i].name);
	}
	free(equal->control_info);
	LADSPAcontrolUnMMAP(equal->control_data);
	LADSPAunload(equal->library);
	free(equal);
}

static int equal_elem_count(snd_ctl_ext_t *ext)
{
	snd_ctl_equal_t *equal = ext->private_data;
	return equal->num_input_controls;
}

static int equal_elem_list(snd_ctl_ext_t *ext, unsigned int offset,
		snd_ctl_elem_id_t *id)
{
	snd_ctl_equal_t *equal = ext->private_data;
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	snd_ctl_elem_id_set_name(id, equal->control_info[offset].name);
	snd_ctl_elem_id_set_device(id, offset);
	return 0;
}

static snd_ctl_ext_key_t equal_find_elem(snd_ctl_ext_t *ext,
		const snd_ctl_elem_id_t *id)
{
	snd_ctl_equal_t *equal = ext->private_data;
	const char *name;
	unsigned int i, key;

	name = snd_ctl_elem_id_get_name(id);

	for (i = 0; i < equal->num_input_controls; i++) {
		key = i;
		if (!strcmp(name, equal->control_info[key].name)) {
			return key;
		}
	}

	return SND_CTL_EXT_KEY_NOT_FOUND;
}

static int equal_get_attribute(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key,
		int *type, unsigned int *acc, unsigned int *count)
{
	snd_ctl_equal_t *equal = ext->private_data;
	*type = SND_CTL_ELEM_TYPE_INTEGER;
	*acc = SND_CTL_EXT_ACCESS_READWRITE;
	*count = equal->control_data->channels;
	return 0;
}

static int equal_get_integer_info(snd_ctl_ext_t *ext,
	snd_ctl_ext_key_t key, long *imin, long *imax, long *istep)
{
	*istep = 1;
	*imin = 0;
	*imax = 100;
	return 0;
}

static int equal_read_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key,
		long *value)
{
	snd_ctl_equal_t *equal = ext->private_data;
	int i;

	for(i = 0; i < equal->control_data->channels; i++) {
		value[i] = ((equal->control_data->control[key].data[i] -
			equal->control_info[key].min)/
			(equal->control_info[key].max-
			equal->control_info[key].min))*100;
	}

	return equal->control_data->channels*sizeof(long);
}

static int equal_write_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key,
		long *value)
{
	snd_ctl_equal_t *equal = ext->private_data;
	int i;
	float setting;

	for(i = 0; i < equal->control_data->channels; i++) {
		setting = value[i];
		equal->control_data->control[key].data[i] = (setting/100)*
			(equal->control_info[key].max-
			equal->control_info[key].min)+
			equal->control_info[key].min;
	}

	return 1;
}

static int equal_read_event(snd_ctl_ext_t *ext ATTRIBUTE_UNUSED,
		snd_ctl_elem_id_t *id ATTRIBUTE_UNUSED,
		unsigned int *event_mask ATTRIBUTE_UNUSED)
{
	return -EAGAIN;
}

static snd_ctl_ext_callback_t equal_ext_callback = {
	.close = equal_close,
	.elem_count = equal_elem_count,
	.elem_list = equal_elem_list,
	.find_elem = equal_find_elem,
	.get_attribute = equal_get_attribute,
	.get_integer_info = equal_get_integer_info,
	.read_integer = equal_read_integer,
	.write_integer = equal_write_integer,
	.read_event = equal_read_event,
};

SND_CTL_PLUGIN_DEFINE_FUNC(equal)
{
	/* TODO: Plug all of the memory leaks if these some initialization
		failure */
	snd_config_iterator_t it, next;
	snd_ctl_equal_t *equal;
	const char *controls = ".alsaequal.bin";
	const char *library = "/usr/lib/ladspa/caps.so";
	const char *module = "Eq10";
	long channels = 2;
	const char *sufix = " Playback Volume";
	int err, i, index;

	/* Parse configuration options from asoundrc */
	snd_config_for_each(it, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(it);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0)
			continue;
		if (strcmp(id, "controls") == 0) {
			snd_config_get_string(n, &controls);
			continue;
		}
		if (strcmp(id, "library") == 0) {
			snd_config_get_string(n, &library);
			continue;
		}
		if (strcmp(id, "module") == 0) {
			snd_config_get_string(n, &module);
			continue;
		}
		if (strcmp(id, "channels") == 0) {
			snd_config_get_integer(n, &channels);
			if(channels < 1) {
				SNDERR("channels < 1");
				return -EINVAL;
			}
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	/* Intialize the local object data */
	equal = calloc(1, sizeof(*equal));
	if (equal == NULL)
		return -ENOMEM;

	equal->ext.version = SND_CTL_EXT_VERSION;
	equal->ext.card_idx = 0;
	equal->ext.poll_fd = -1;
	equal->ext.callback = &equal_ext_callback;
	equal->ext.private_data = equal;

	/* Open the LADSPA Plugin */
	equal->library = LADSPAload(library);
	if(equal->library == NULL) {
		return -1;
	}

	equal->klass = LADSPAfind(equal->library, library, module);
	if(equal->klass == NULL) {
		return -1;
	}

	/* Import data from the LADSPA Plugin */
	strncpy(equal->ext.id, equal->klass->Label, sizeof(equal->ext.id));
	strncpy(equal->ext.driver, "LADSPA Plugin", sizeof(equal->ext.driver));
	strncpy(equal->ext.name, equal->klass->Label, sizeof(equal->ext.name));
	strncpy(equal->ext.longname, equal->klass->Name,
			sizeof(equal->ext.longname));
	strncpy(equal->ext.mixername, "alsaequal", sizeof(equal->ext.mixername));

	/* Create the ALSA External Plugin */
	err = snd_ctl_ext_create(&equal->ext, name, SND_CTL_NONBLOCK);
	if (err < 0) {
		return -1;
	}

	/* MMAP to the controls file */
	equal->control_data = LADSPAcontrolMMAP(equal->klass, controls, channels);
	if(equal->control_data == NULL) {
		return -1;
	}
	
	equal->num_input_controls = 0;
	for(i = 0; i < equal->control_data->num_controls; i++) {
		if(equal->control_data->control[i].type == LADSPA_CNTRL_INPUT) {
			equal->num_input_controls++;
		}
	}
	
	/* Pull in data from controls file */
	equal->control_info = malloc(
			sizeof(snd_ctl_equal_control_t)*equal->num_input_controls);
	if(equal->control_info == NULL) {
		return -1;
	}

	for(i = 0; i < equal->num_input_controls; i++) {
		if(equal->control_data->control[i].type == LADSPA_CNTRL_INPUT) {
			index = equal->control_data->control[i].index;
			if(equal->klass->PortDescriptors[index] !=
					(LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL)) {
				SNDERR("Problem with control file %s, %d.", controls, index);
				return -1;
			}
			equal->control_info[i].min =
					equal->klass->PortRangeHints[index].LowerBound;
			equal->control_info[i].max =
					equal->klass->PortRangeHints[index].UpperBound;
			equal->control_info[i].name = malloc(
					strlen(equal->klass->PortNames[index]) +
					strlen(sufix) + 6);
			if(equal->control_info[i].name == NULL) {
				return -1;
			}
			sprintf(equal->control_info[i].name, "%02d. %s%s",
					index, equal->klass->PortNames[index], sufix);
		}
	}

	/* Make sure that the control file makes sense */
	if(equal->klass->PortDescriptors[equal->control_data->input_index] !=
			(LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO)) {
		SNDERR("Problem with control file %s.", controls);
		return -1;
	}
	if(equal->klass->PortDescriptors[equal->control_data->output_index] !=
			(LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO)) {
		SNDERR("Problem with control file %s.", controls);
		return -1;
	}

	*handlep = equal->ext.handle;
	return 0;

}

SND_CTL_PLUGIN_SYMBOL(equal);
