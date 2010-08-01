/*
 *  This file is part of pom-ng.
 *  Copyright (C) 2010 Guy Martin <gmsoft@tuxicoman.be>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "common.h"
#include "xmlrpcsrv.h"
#include "xmlrpccmd_input.h"
#include "httpd.h"

#include "input_ipc.h"

#define XMLRPCCMD_INPUT_NUM 5
static struct xmlrpcsrv_command xmlrpccmd_input_commands[XMLRPCCMD_INPUT_NUM] = {

	{
		.name = "input.loadModule",
		.callback_func = xmlrpccmd_input_mod_load,
		.signature = "i:s",
		.help = "Load a module in the input process",
	},

	{
		.name = "input.add",
		.callback_func = xmlrpccmd_input_add,
		.signature = "i:s",
		.help = "Add an input",
	},

	{
		.name = "input.remove",
		.callback_func = xmlrpccmd_input_remove,
		.signature = "i:i",
		.help = "Remove an input",
	},

	{
		.name = "input.start",
		.callback_func = xmlrpccmd_input_start,
		.signature = "i:i",
		.help = "Start an input",
	},

	{
		.name = "input.stop",
		.callback_func = xmlrpccmd_input_stop,
		.signature = "i:i",
		.help = "Stop an input",
	},

};

int xmlrpccmd_input_register_all() {

	int i;

	for (i = 0; i < XMLRPCCMD_INPUT_NUM; i++) {
		if (xmlrpcsrv_register_command(&xmlrpccmd_input_commands[i]) == POM_ERR)
			return POM_ERR;
	}

	return POM_OK;

}

xmlrpc_value *xmlrpccmd_input_mod_load(xmlrpc_env * const envP, xmlrpc_value * const paramArrayP, void * const userData) {


	char *name = NULL;
	xmlrpc_decompose_value(envP, paramArrayP, "(s)", &name);

	if (envP->fault_occurred)
		return NULL;

	if (input_ipc_cmd_mod_load(name) != POM_OK) {
		xmlrpc_faultf(envP, "Unable to load module %s", name);
		free(name);
		return NULL;
	}

	free(name);

	return xmlrpc_int_new(envP, 0);

}

xmlrpc_value *xmlrpccmd_input_add(xmlrpc_env * const envP, xmlrpc_value * const paramArrayP, void * const userData) {

	char *name = NULL;
	xmlrpc_decompose_value(envP, paramArrayP, "(s)", &name);

	if (envP->fault_occurred)
		return NULL;

	int id = input_ipc_cmd_add(name);
	if (id == POM_ERR) {
		xmlrpc_faultf(envP, "Unable to add input %s", name);
		free(name);
		return NULL;
	}
	free(name);
	return xmlrpc_int_new(envP, id);

}

xmlrpc_value *xmlrpccmd_input_remove(xmlrpc_env * const envP, xmlrpc_value * const paramArrayP, void * const userData) {

	int id = 0;
	xmlrpc_decompose_value(envP, paramArrayP, "(i)", &id);

	if (envP->fault_occurred)
		return NULL;
	if (input_ipc_cmd_remove(id) == POM_ERR) {
		xmlrpc_faultf(envP, "Unable to remove input with id %u", id);
		return NULL;
	}
	return xmlrpc_int_new(envP, 0);

}

xmlrpc_value *xmlrpccmd_input_start(xmlrpc_env * const envP, xmlrpc_value * const paramArrayP, void * const userData) {

	unsigned int input_id;
	xmlrpc_decompose_value(envP, paramArrayP, "(i)", &input_id);

	if (envP->fault_occurred)
		return NULL;

	if (input_ipc_cmd_start(input_id)  == POM_ERR) {
		xmlrpc_faultf(envP, "Unable to start input with id %u", input_id);
		return NULL;
	}
	return xmlrpc_int_new(envP, 0);
}

xmlrpc_value *xmlrpccmd_input_stop(xmlrpc_env * const envP, xmlrpc_value * const paramArrayP, void * const userData) {

	unsigned int input_id;
	xmlrpc_decompose_value(envP, paramArrayP, "(i)", &input_id);

	if (envP->fault_occurred)
		return NULL;

	if (input_ipc_cmd_stop(input_id)  == POM_ERR) {
		xmlrpc_faultf(envP, "Unable to stop input with id %u", input_id);
		return NULL;
	}
	return xmlrpc_int_new(envP, 0);
}

