/*
** Zabbix
** Copyright (C) 2000-2011 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"
#if defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL)
#include "log.h"
#include "zbxjson.h"
#include "zbxalgo.h"
#include "checks_simple_vmware.h"
#include"../vmware/vmware.h"

static int	vmware_set_powerstate_result(AGENT_RESULT *result)
{
	int	ret = SYSINFO_RET_OK;

	if (NULL != GET_STR_RESULT(result))
	{
		if (0 == strcmp(result->str, "poweredOff"))
			SET_UI64_RESULT(result, 0);
		else if (0 == strcmp(result->str, "poweredOn"))
			SET_UI64_RESULT(result, 1);
		else if (0 == strcmp(result->str, "suspended"))
			SET_UI64_RESULT(result, 2);
		else
			ret = SYSINFO_RET_FAIL;

		UNSET_STR_RESULT(result);
	}

	return ret;
}

static zbx_vmware_hv_t	*hv_get(zbx_vector_ptr_t *hvs, const char *uuid)
{
	int	i;

	for (i = 0; i < hvs->values_num; i++)
	{
		zbx_vmware_hv_t	*hv = (zbx_vmware_hv_t *)hvs->values[i];

		if (0 == strcmp(hv->uuid, uuid))
			return hv;
	}

	return NULL;
}

static zbx_vmware_vm_t	*vm_get(zbx_vector_ptr_t *vms, const char *uuid)
{
	int		i;

	for (i = 0; i < vms->values_num; i++)
	{
		zbx_vmware_vm_t	*vm = (zbx_vmware_vm_t *)vms->values[i];

		if (0 == strcmp(vm->uuid, uuid))
			return vm;
	}

	return NULL;
}

static zbx_vmware_vm_t	*service_vm_get(zbx_vmware_service_t *service, const char *uuid)
{
	int	i;

	for (i = 0; i < service->data->hvs.values_num; i++)
	{
		zbx_vmware_vm_t	*vm;
		zbx_vmware_hv_t	*hv = (zbx_vmware_hv_t *)service->data->hvs.values[i];

		if (NULL != (vm = vm_get(&hv->vms, uuid)))
			return vm;
	}

	return NULL;
}

static zbx_vmware_cluster_t	*cluster_get(zbx_vector_ptr_t *clusters, const char *clusterid)
{
	int	i;

	for (i = 0; i < clusters->values_num; i++)
	{
		zbx_vmware_cluster_t	*cluster = (zbx_vmware_cluster_t *)clusters->values[i];

		if (0 == strcmp(cluster->id, clusterid))
			return cluster;
	}

	return NULL;
}

static zbx_vmware_cluster_t	*cluster_get_by_name(zbx_vector_ptr_t *clusters, const char *name)
{
	int	i;

	for (i = 0; i < clusters->values_num; i++)
	{
		zbx_vmware_cluster_t	*cluster = (zbx_vmware_cluster_t *)clusters->values[i];

		if (0 == strcmp(cluster->name, name))
			return cluster;
	}

	return NULL;
}


static int	vmware_counter_get(const char *stats, const char *instance, zbx_uint64_t counterid, int coeff,
		AGENT_RESULT *result)
{
	int		ret = SYSINFO_RET_FAIL;
	char		xpath[MAX_STRING_LEN], *value;
	zbx_uint64_t	value_ui64;

	zbx_snprintf(xpath, sizeof(xpath), ZBX_XPATH_LN3("value", "id", "counterId") "[.='" ZBX_FS_UI64 "']/.."
				ZBX_XPATH_LN("instance") "[.='%s']/../.." ZBX_XPATH_LN("value"), counterid, instance);

	if (NULL == (value = zbx_xml_read_value(stats, xpath)))
		return SYSINFO_RET_FAIL;

	if (SUCCEED == is_uint64(value, &value_ui64))
	{
		SET_UI64_RESULT(result, value_ui64 * coeff);

		ret = SYSINFO_RET_OK;
	}

	zbx_free(value);

	return ret;
}

static int	vmware_service_get_vm_counter(zbx_vmware_service_t *service, const char *uuid, const char *instance,
		zbx_uint64_t counterid, int coeff, AGENT_RESULT *result)
{
	int			i, ret = SYSINFO_RET_FAIL;
	zbx_vmware_vm_t		*vm = NULL;
	zbx_vmware_dev_t	*dev;

	if (NULL == (vm = service_vm_get(service, uuid)))
		return SYSINFO_RET_FAIL;

	for (i = 0; i < vm->devs.values_num; i++)
	{
		dev = (zbx_vmware_dev_t *)vm->devs.values[i];

		if (0 == strcmp(dev->instance, instance))
			break;
	}

	if (i == vm->devs.values_num)
		return SYSINFO_RET_FAIL;

	ret = vmware_counter_get(vm->stats, instance, counterid, coeff, result);

	return ret;
}

int	vmware_get_events(const char *events, zbx_uint64_t lastlogsize, AGENT_RESULT *result)
{
	zbx_vector_str_t	keys;
	zbx_uint64_t		key;
	char			*value, xpath[MAX_STRING_LEN];
	int			i;
	zbx_log_t		*log;
	struct tm		tm;
	time_t			t;

	zbx_vector_str_create(&keys);

	if (SUCCEED != zbx_xml_read_values(events, ZBX_XPATH_LN2("Event", "key"), &keys))
	{
		zbx_vector_str_destroy(&keys);
		return SYSINFO_RET_FAIL;
	}

	for (i = keys.values_num - 1; i >= 0; i--)
	{
		if (SUCCEED != is_uint64(keys.values[i], &key))
			continue;

		if (key <= lastlogsize)
			continue;

		/* value */

		zbx_snprintf(xpath, sizeof(xpath), ZBX_XPATH_LN2("Event", "key") "[.='" ZBX_FS_UI64 "']/.."
				ZBX_XPATH_LN("fullFormattedMessage"), key);

		if (NULL == (value = zbx_xml_read_value(events, xpath)))
			continue;

		zbx_replace_invalid_utf8(value);
		log = add_log_result(result, value);
		log->logeventid = key;
		log->lastlogsize = key;

		zbx_free(value);

		/* timestamp */

		zbx_snprintf(xpath, sizeof(xpath), ZBX_XPATH_LN2("Event", "key") "[.='" ZBX_FS_UI64 "']/.."
				ZBX_XPATH_LN("createdTime"), key);

		if (NULL == (value = zbx_xml_read_value(events, xpath)))
			continue;

		/* 2013-06-04T14:19:23.406298Z */
		if (6 == sscanf(value, "%d-%d-%dT%d:%d:%d.%*s", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
				&tm.tm_min, &tm.tm_sec))

		{
			int		tz_offset;
#if defined(HAVE_TM_TM_GMTOFF)
			struct tm	*ptm;
			time_t		now;

			now = time(NULL);
			ptm = localtime(&now);
			tz_offset = ptm->tm_gmtoff;
#else
			tz_offset = -timezone;
#endif
			tm.tm_year -= 1900;
			tm.tm_mon--;
			tm.tm_isdst = -1;

			if (0 < (t = mktime(&tm)))
				log->timestamp = (int)t + tz_offset;
		}
		zbx_free(value);
	}

	if (!ISSET_LOG(result))
		set_log_result_empty(result);

	zbx_vector_str_clean(&keys);
	zbx_vector_str_destroy(&keys);

	return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: get_vmware_service                                               *
 *                                                                            *
 * Purpose: gets vmware service object                                        *
 *                                                                            *
 * Parameters: url       - [IN] the vmware service URL                        *
 *             username  - [IN] the vmware service username                   *
 *             password  - [IN] the vmware service password                   *
 *             ret       - [OUT] the operation result code                    *
 *                                                                            *
 * Return value: The vmware service object or NULL if the service was not     *
 *               found, did not have data or any error occured. In the last   *
 *               case the error message will be stored in agent result.       *
 *                                                                            *
 * Comments: There are three possible cases:                                  *
 *             1) the vmware service is not ready. This can happen when       *
 *                service was added, but not yet processed by collector.      *
 *                In this case NULL is returned and result code is set to     *
 *                SYSINFO_RET_OK.                                             *
 *             2) the vmware service update failed. This can happen if there  *
 *                was a network problem, authentication failure or any error  *
 *                that prevented from obtaining and parsing vmware data.      *
 *                In this case NULL is returned and result code is set to     *
 *                SYSINFO_RET_FAIL.                                           *
 *             3) the vmware service has been updated successfully.           *
 *                In this case the service object is returned and result code *
 *                is not set.                                                 *
 *                                                                            *
 ******************************************************************************/
static zbx_vmware_service_t *get_vmware_service(const char *url, const char *username, const char *password,
		AGENT_RESULT *result, int *ret)
{
	zbx_vmware_service_t	*service;

	if (NULL == (service = zbx_vmware_get_service(url, username, password)))
	{
		*ret = SYSINFO_RET_OK;
		return NULL;
	}

	if (0 != (service->state & ZBX_VMWARE_STATE_FAILED))
	{
		if (NULL != service->data->error)
			SET_MSG_RESULT(result, zbx_strdup(NULL, service->data->error));

		*ret = SYSINFO_RET_FAIL;
		return NULL;
	}

	return service;
}

/******************************************************************************
 *                                                                            *
 * Function: get_vcenter_vmstat                                               *
 *                                                                            *
 * Purpose: retrieves data from virtual machine details                       *
 *                                                                            *
 * Parameters: request   - [IN] the original request. The first parameter is  *
 *                              vmware service URL and the second parameter   *
 *                              is virtual machine uuid.                      *
 *             username  - [IN] the vmware service user name                  *
 *             password  - [IN] the vmware service password                   *
 *             xpath     - [IN] the xpath describing data to retrieve         *
 *             result    - [OUT] the request result                           *
 *                                                                            *
 ******************************************************************************/
static int	get_vcenter_vmstat(AGENT_REQUEST *request, const char *username, const char *password,
		const char *xpath, AGENT_RESULT *result)
{
	zbx_vmware_service_t	*service;
	zbx_vmware_vm_t		*vm = NULL;
	char			*url, *uuid, *value;
	int			ret = SYSINFO_RET_FAIL;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (vm = service_vm_get(service, uuid)))
		goto unlock;

	if (NULL == (value = zbx_xml_read_value(vm->details, xpath)))
		goto unlock;

	SET_STR_RESULT(result, value);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

#define ZBX_OPT_XPATH		0
#define ZBX_OPT_VM_NUM		1
#define ZBX_OPT_MEM_BALLOONED	2

static int	hv_get_stat(zbx_vmware_hv_t *hv, int opt, const char *xpath, AGENT_RESULT *result)
{
	char		*value;
	zbx_uint64_t	value_uint64, value_uint64_sum;
	int		i;

	switch (opt)
	{
		case ZBX_OPT_XPATH:
			if (NULL == (value = zbx_xml_read_value(hv->details, xpath)))
				return SYSINFO_RET_FAIL;

			SET_STR_RESULT(result, value);
			break;
		case ZBX_OPT_VM_NUM:
			SET_UI64_RESULT(result, hv->vms.values_num);
			break;
		case ZBX_OPT_MEM_BALLOONED:
			xpath = ZBX_XPATH_LN2("quickStats", "balloonedMemory");
			value_uint64_sum = 0;

			for (i = 0; i < hv->vms.values_num; i++)
			{
				zbx_vmware_vm_t	*vm = (zbx_vmware_vm_t *)hv->vms.values[i];

				if (NULL == (value = zbx_xml_read_value(vm->details, xpath)))
					continue;

				if (SUCCEED == is_uint64(value, &value_uint64))
					value_uint64_sum += value_uint64;

				zbx_free(value);
			}

			SET_UI64_RESULT(result, value_uint64_sum);
			break;
	}

	return SYSINFO_RET_OK;
}

/******************************************************************************
 *                                                                            *
 * Function: get_vcenter_stat                                                 *
 *                                                                            *
 * Purpose: retrieves data from hypervisor details                            *
 *                                                                            *
 * Parameters: request   - [IN] the original request. The first parameter is  *
 *                              vmware service URL and the second parameter   *
 *                              is hypervisor uuid.                           *
 *             username  - [IN] the vmware service user name                  *
 *             password  - [IN] the vmware service password                   *
 *             xpath     - [IN] the xpath describing data to retrieve         *
 *             result    - [OUT] the request result                           *
 *                                                                            *
 ******************************************************************************/
static int	get_vcenter_stat(AGENT_REQUEST *request, const char *username, const char *password,
		int opt, const char *xpath, AGENT_RESULT *result)
{
	zbx_vmware_service_t	*service;
	int			ret = SYSINFO_RET_FAIL;
	const char		*uuid, *url;
	zbx_vmware_hv_t		*hv;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
		goto unlock;

	ret = hv_get_stat(hv, opt, xpath, result);
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_cluster_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	struct zbx_json		json_data;
	int			i, ret = SYSINFO_RET_FAIL;
	char			*url;
	zbx_vmware_service_t	*service;

	if (1 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < service->data->clusters.values_num; i++)
	{
		zbx_vmware_cluster_t	*cluster = (zbx_vmware_cluster_t *)service->data->clusters.values[i];

		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#CLUSTER.ID}", cluster->id, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#CLUSTER.NAME}", cluster->name, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_cluster_status(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *name, *status;
	zbx_vmware_service_t	*service;
	zbx_vmware_cluster_t	*cluster;
	int			ret = SYSINFO_RET_FAIL;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	name = get_rparam(request, 1);

	if ('\0' == *name)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (cluster = cluster_get_by_name(&service->data->clusters, name)))
		goto unlock;

	if (NULL == (status = zbx_xml_read_value(cluster->status, ZBX_XPATH_LN2("val", "overallStatus"))))
		goto unlock;

	ret = SYSINFO_RET_OK;

	if (0 == strcmp(status, "gray"))
		SET_UI64_RESULT(result, 0);
	else if (0 == strcmp(status, "green"))
		SET_UI64_RESULT(result, 1);
	else if (0 == strcmp(status, "yellow"))
		SET_UI64_RESULT(result, 2);
	else if (0 == strcmp(status, "red"))
		SET_UI64_RESULT(result, 3);
	else
		ret =  SYSINFO_RET_FAIL;

	zbx_free(status);
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_eventlog(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url;
	int			ret = SYSINFO_RET_FAIL;
	zbx_vmware_service_t	*service;

	if (1 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	ret = vmware_get_events(service->data->events, request->lastlogsize, result);
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_version(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *version;
	int			ret = SYSINFO_RET_FAIL;
	zbx_vmware_service_t	*service;

	if (1 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (version = zbx_xml_read_value(service->contents, ZBX_XPATH_LN2("about", "version"))))
		goto unlock;

	SET_STR_RESULT(result, version);

	ret = SYSINFO_RET_OK;

unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_fullname(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *fullname;
	int			ret = SYSINFO_RET_FAIL;
	zbx_vmware_service_t	*service;

	if (1 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (fullname = zbx_xml_read_value(service->contents, ZBX_XPATH_LN2("about", "fullName"))))
		goto unlock;

	SET_STR_RESULT(result, fullname);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_hv_cluster_name(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *uuid;
	zbx_vmware_hv_t		*hv;
	zbx_vmware_service_t	*service;
	int			ret = SYSINFO_RET_FAIL;
	zbx_vmware_cluster_t	*cluster = NULL;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
		goto unlock;

	if (NULL != hv->clusterid)
		cluster = cluster_get(&service->data->clusters, hv->clusterid);

	SET_STR_RESULT(result, zbx_strdup(NULL, NULL != cluster ? cluster->name : ""));

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_hv_cpu_usage(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_stat(request, username, password, ZBX_OPT_XPATH,
			ZBX_XPATH_LN2("quickStats", "overallCpuUsage"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * 1000000;

	return ret;
}

int	check_vcenter_hv_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	struct zbx_json		json_data;
	int			i, ret = SYSINFO_RET_FAIL;
	char			*url, *name;
	zbx_vmware_service_t	*service;

	if (1 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < service->data->hvs.values_num; i++)
	{
		zbx_vmware_cluster_t	*cluster = NULL;
		zbx_vmware_hv_t	*hv = (zbx_vmware_hv_t *)service->data->hvs.values[i];

		if (NULL == (name = zbx_xml_read_value(hv->details, ZBX_XPATH_LN2("config", "name"))))
			continue;

		if (NULL != hv->clusterid)
			cluster = cluster_get(&service->data->clusters, hv->clusterid);

		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#HV.UUID}", hv->uuid, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#HV.ID}", hv->id, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#HV.NAME}", name, ZBX_JSON_TYPE_STRING);
		zbx_json_addstring(&json_data, "{#CLUSTER.NAME}",
				(NULL != cluster ? cluster->name : ""), ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);

		zbx_free(name);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_hv_fullname(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("product", "fullName"),
			result);
}

int	check_vcenter_hv_hw_cpu_num(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("hardware", "numCpuCores"),
			result);
}

int	check_vcenter_hv_hw_cpu_freq(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("hardware", "cpuMhz"),
			result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * 1000000;

	return ret;
}

int	check_vcenter_hv_hw_cpu_model(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("hardware", "cpuModel"),
			result);
}

int	check_vcenter_hv_hw_cpu_threads(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH,
			ZBX_XPATH_LN2("hardware", "numCpuThreads"), result);
}

int	check_vcenter_hv_hw_memory(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("hardware", "memorySize"),
			result);
}

int	check_vcenter_hv_hw_model(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("hardware", "model"),
			result);
}

int	check_vcenter_hv_hw_uuid(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("hardware", "uuid"),
			result);
}

int	check_vcenter_hv_hw_vendor(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("hardware", "vendor"),
			result);
}

int	check_vcenter_hv_memory_size_ballooned(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_stat(request, username, password, ZBX_OPT_MEM_BALLOONED, NULL, result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_hv_memory_used(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_stat(request, username, password, ZBX_OPT_XPATH,
			ZBX_XPATH_LN2("quickStats", "overallMemoryUsage"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_hv_status(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("val", "overallStatus"),
			result);

	if (SYSINFO_RET_OK == ret && NULL != GET_STR_RESULT(result))
	{
		if (0 == strcmp(result->str, "gray"))
			SET_UI64_RESULT(result, 0);
		else if (0 == strcmp(result->str, "green"))
			SET_UI64_RESULT(result, 1);
		else if (0 == strcmp(result->str, "yellow"))
			SET_UI64_RESULT(result, 2);
		else if (0 == strcmp(result->str, "red"))
			SET_UI64_RESULT(result, 3);
		else
			ret = SYSINFO_RET_FAIL;

		UNSET_STR_RESULT(result);
	}

	return ret;
}

int	check_vcenter_hv_uptime(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("quickStats", "uptime"),
			result);
}

int	check_vcenter_hv_version(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_XPATH, ZBX_XPATH_LN2("product", "version"),
			result);
}

int	check_vcenter_hv_vm_num(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_stat(request, username, password, ZBX_OPT_VM_NUM, NULL, result);
}

int	check_vcenter_hv_network_in(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *mode, *uuid;
	zbx_vmware_service_t	*service;
	int			ret = SYSINFO_RET_FAIL;
	zbx_vmware_hv_t		*hv;

	if (2 > request->nparam || request->nparam > 3)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	mode = get_rparam(request, 2);

	if (NULL != mode && '\0' != *mode && 0 != strcmp(mode, "bps"))
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
		goto unlock;

	ret = vmware_counter_get(hv->stats, "", service->counters.nic_received, ZBX_KIBIBYTE, result);
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_hv_network_out(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *mode, *uuid;
	zbx_vmware_service_t	*service;
	int			ret = SYSINFO_RET_FAIL;
	zbx_vmware_hv_t		*hv;

	if (2 > request->nparam || request->nparam > 3)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	mode = get_rparam(request, 2);

	if (NULL != mode && '\0' != *mode && 0 != strcmp(mode, "bps"))
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
		goto unlock;

	ret = vmware_counter_get(hv->stats, "", service->counters.nic_transmitted, ZBX_KIBIBYTE, result);
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_hv_datastore_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *uuid;
	zbx_vmware_service_t	*service;
	zbx_vmware_hv_t		*hv;
	struct zbx_json		json_data;
	int			i, ret = SYSINFO_RET_FAIL;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
		goto unlock;

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < hv->datastores.values_num; i++)
	{
		zbx_vmware_datastore_t	*datastore = hv->datastores.values[i];

		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#DATASTORE}", datastore->name, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_hv_datastore_read(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *mode, *uuid, *name;
	zbx_vmware_service_t	*service;
	zbx_vmware_hv_t		*hv;
	int			i, ret = SYSINFO_RET_FAIL;

	if (3 > request->nparam || request->nparam > 4)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	name = get_rparam(request, 2);
	mode = get_rparam(request, 3);

	if (NULL != mode && '\0' != *mode && 0 != strcmp(mode, "latency"))
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
		goto unlock;

	for (i = 0; i < hv->datastores.values_num; i++)
	{
		zbx_vmware_datastore_t	*datastore = hv->datastores.values[i];

		if (0 == strcmp(name, datastore->name))
		{
			if (NULL == datastore->uuid)
				break;

			ret = vmware_counter_get(hv->stats, datastore->uuid,
					service->counters.datastore_read_latency, 1, result);
			goto unlock;
		}
	}

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_hv_datastore_write(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *mode, *uuid, *name;
	zbx_vmware_service_t	*service;
	zbx_vmware_hv_t		*hv;
	int			i, ret = SYSINFO_RET_FAIL;

	if (3 > request->nparam || request->nparam > 4)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	name = get_rparam(request, 2);
	mode = get_rparam(request, 3);

	if (NULL != mode && '\0' != *mode && 0 != strcmp(mode, "latency"))
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (hv = hv_get(&service->data->hvs, uuid)))
		goto unlock;

	for (i = 0; i < hv->datastores.values_num; i++)
	{
		zbx_vmware_datastore_t	*datastore = hv->datastores.values[i];

		if (0 == strcmp(name, datastore->name))
		{
			if (NULL == datastore->uuid)
				break;

			ret = vmware_counter_get(hv->stats, datastore->uuid,
					service->counters.datastore_write_latency, 1, result);
			goto unlock;
		}
	}

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_cpu_num(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("config", "numCpu"), result);
}

int	check_vcenter_vm_cluster_name(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int			i, ret = SYSINFO_RET_FAIL;
	char			*url, *uuid;
	zbx_vmware_service_t	*service;
	zbx_vmware_cluster_t	*cluster = NULL;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	for (i = 0; i < service->data->hvs.values_num; i++)
	{
		zbx_vmware_hv_t	*hv = service->data->hvs.values[i];
		zbx_vmware_vm_t		*vm;

		if (NULL != (vm = vm_get(&hv->vms, uuid)))
		{
			if (NULL != hv->clusterid)
				cluster = cluster_get(&service->data->clusters, hv->clusterid);

			break;
		}
	}

	SET_STR_RESULT(result, zbx_strdup(NULL, NULL != cluster ? cluster->name : ""));

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_cpu_usage(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("quickStats", "overallCpuUsage"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * 1000000;

	return ret;
}

int	check_vcenter_vm_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	struct zbx_json		json_data;
	int			i, k, ret = SYSINFO_RET_FAIL;
	char			*url, *vm_name, *hv_name;
	zbx_vmware_service_t	*service;
	zbx_vmware_hv_t		*hv;
	zbx_vmware_vm_t		*vm;

	if (1 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < service->data->hvs.values_num; i++)
	{
		zbx_vmware_cluster_t	*cluster = NULL;

		hv = (zbx_vmware_hv_t *)service->data->hvs.values[i];

		if (NULL != hv->clusterid)
			cluster = cluster_get(&service->data->clusters, hv->clusterid);

		for (k = 0; k < hv->vms.values_num; k++)
		{
			vm = (zbx_vmware_vm_t *)hv->vms.values[k];

			if (NULL == (vm_name = zbx_xml_read_value(vm->details, ZBX_XPATH_LN2("config", "name"))))
				continue;

			if (NULL == (hv_name = zbx_xml_read_value(hv->details, ZBX_XPATH_LN2("config", "name"))))
			{
				zbx_free(vm_name);
				continue;
			}

			zbx_json_addobject(&json_data, NULL);
			zbx_json_addstring(&json_data, "{#VM.UUID}", vm->uuid, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json_data, "{#VM.ID}", vm->id, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json_data, "{#VM.NAME}", vm_name, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json_data, "{#HV.NAME}", hv_name, ZBX_JSON_TYPE_STRING);
			zbx_json_addstring(&json_data, "{#CLUSTER.NAME}",
					(NULL != cluster ? cluster->name : ""), ZBX_JSON_TYPE_STRING);
			zbx_json_close(&json_data);

			zbx_free(hv_name);
			zbx_free(vm_name);
		}
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_hv_name(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int			ret = SYSINFO_RET_FAIL;
	zbx_vmware_service_t	*service;
	zbx_vmware_hv_t		*hv;
	char			*url, *uuid, *name;
	int			i;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	for (i = 0; i < service->data->hvs.values_num; i++)
	{
		hv = (zbx_vmware_hv_t *)service->data->hvs.values[i];

		if (NULL != vm_get(&hv->vms, uuid))
			break;
	}

	if (i != service->data->hvs.values_num)
	{
		name = zbx_xml_read_value(hv->details, ZBX_XPATH_LN2("config", "name"));

		SET_STR_RESULT(result, name);
		ret = SYSINFO_RET_OK;
	}
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_memory_size(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("config", "memorySizeMB"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_vm_memory_size_ballooned(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("quickStats", "balloonedMemory"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_vm_memory_size_compressed(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("quickStats", "compressedMemory"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_vm_memory_size_swapped(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("quickStats", "swappedMemory"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_vm_memory_size_usage_guest(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("quickStats", "guestMemoryUsage"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_vm_memory_size_usage_host(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("quickStats", "hostMemoryUsage"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_vm_memory_size_private(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("quickStats", "privateMemory"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_vm_memory_size_shared(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("quickStats", "sharedMemory"), result);

	if (SYSINFO_RET_OK == ret && NULL != GET_UI64_RESULT(result))
		result->ui64 = result->ui64 * ZBX_MEBIBYTE;

	return ret;
}

int	check_vcenter_vm_powerstate(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	int	ret;

	ret = get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("runtime", "powerState"), result);

	if (SYSINFO_RET_OK == ret)
		ret = vmware_set_powerstate_result(result);

	return ret;
}

int	check_vcenter_vm_net_if_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	struct zbx_json		json_data;
	zbx_vmware_service_t	*service;
	zbx_vmware_vm_t		*vm = NULL;
	zbx_vmware_dev_t	*dev;
	char			*url, *uuid;
	int			i, ret = SYSINFO_RET_FAIL;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (vm = service_vm_get(service, uuid)))
		goto unlock;

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < vm->devs.values_num; i++)
	{
		dev = (zbx_vmware_dev_t *)vm->devs.values[i];

		if (ZBX_VMWARE_DEV_TYPE_NIC != dev->type)
			continue;

		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#IFNAME}", dev->instance, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_net_if_in(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *uuid, *instance, *mode;
	zbx_vmware_service_t	*service;
	zbx_uint64_t		counterid;
	int 			coeff, ret = SYSINFO_RET_FAIL;

	if (3 > request->nparam || request->nparam > 4)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	instance = get_rparam(request, 2);
	mode = get_rparam(request, 3);

	if ('\0' == *uuid || '\0' == *instance)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bps"))
	{
		counterid = service->counters.nic_received;
		coeff = ZBX_KIBIBYTE;
	}
	else if (0 == strcmp(mode, "pps"))
	{
		counterid = service->counters.nic_packets_rx;
		coeff = 1;
	}
	else
		goto unlock;

	ret = vmware_service_get_vm_counter(service, uuid, instance, counterid, coeff, result);
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_net_if_out(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *uuid, *instance, *mode;
	zbx_vmware_service_t	*service;
	zbx_uint64_t		counterid;
	int 			coeff, ret = SYSINFO_RET_FAIL;

	if (3 > request->nparam || request->nparam > 4)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	instance = get_rparam(request, 2);
	mode = get_rparam(request, 3);

	if ('\0' == *uuid || '\0' == *instance)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bps"))
	{
		counterid = service->counters.nic_transmitted;
		coeff = ZBX_KIBIBYTE;
	}
	else if (0 == strcmp(mode, "pps"))
	{
		counterid = service->counters.nic_packets_tx;
		coeff = 1;
	}
	else
		goto unlock;

	ret = vmware_service_get_vm_counter(service, uuid, instance, counterid, coeff, result);
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_storage_committed(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("storage", "committed"), result);
}

int	check_vcenter_vm_storage_unshared(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("storage", "unshared"), result);
}

int	check_vcenter_vm_storage_uncommitted(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("storage", "uncommitted"), result);
}

int	check_vcenter_vm_uptime(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	return get_vcenter_vmstat(request, username, password, ZBX_XPATH_LN2("quickStats", "uptimeSeconds"), result);
}

int	check_vcenter_vm_vfs_dev_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	struct zbx_json		json_data;
	zbx_vmware_service_t	*service;
	zbx_vmware_vm_t		*vm = NULL;
	zbx_vmware_dev_t	*dev;
	char			*url, *uuid;
	int			i, ret = SYSINFO_RET_FAIL;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (vm = service_vm_get(service, uuid)))
		goto unlock;

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < vm->devs.values_num; i++)
	{
		dev = (zbx_vmware_dev_t *)vm->devs.values[i];

		if (ZBX_VMWARE_DEV_TYPE_DISK != dev->type)
			continue;

		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#DISKNAME}", dev->instance, ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_vfs_dev_read(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *uuid, *instance, *mode;
	zbx_vmware_service_t	*service;
	zbx_uint64_t		counterid;
	int			coeff, ret = SYSINFO_RET_FAIL;

	if (3 > request->nparam || request->nparam > 4)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	instance = get_rparam(request, 2);
	mode = get_rparam(request, 3);

	if ('\0' == *uuid || '\0' == *instance)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bps"))
	{
		counterid = service->counters.disk_read;
		coeff = ZBX_KIBIBYTE;
	}
	else if (0 == strcmp(mode, "ops"))
	{
		counterid = service->counters.disk_number_read_averaged;
		coeff = 1;
	}
	else
		goto unlock;

	ret =  vmware_service_get_vm_counter(service, uuid, instance, counterid, coeff, result);
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_vfs_dev_write(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	char			*url, *uuid, *instance, *mode;
	zbx_vmware_service_t	*service;
	zbx_uint64_t		counterid;
	int			coeff, ret = SYSINFO_RET_FAIL;

	if (3 > request->nparam || request->nparam > 4)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	instance = get_rparam(request, 2);
	mode = get_rparam(request, 3);

	if ('\0' == *uuid || '\0' == *instance)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "bps"))
	{
		counterid = service->counters.disk_write;
		coeff = ZBX_KIBIBYTE;
	}
	else if (0 == strcmp(mode, "ops"))
	{
		counterid = service->counters.disk_number_write_averaged;
		coeff = 1;
	}
	else
		goto unlock;

	ret =  vmware_service_get_vm_counter(service, uuid, instance, counterid, coeff, result);
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_vfs_fs_discovery(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	struct zbx_json		json_data;
	zbx_vmware_service_t	*service;
	zbx_vmware_vm_t		*vm = NULL;
	char			*url, *uuid;
	zbx_vector_str_t	disks;
	int			i, ret = SYSINFO_RET_FAIL;

	if (2 != request->nparam)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);

	if ('\0' == *uuid)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (vm = service_vm_get(service, uuid)))
		goto unlock;

	zbx_vector_str_create(&disks);

	zbx_xml_read_values(vm->details, ZBX_XPATH_LN2("disk", "diskPath"), &disks);

	zbx_json_init(&json_data, ZBX_JSON_STAT_BUF_LEN);
	zbx_json_addarray(&json_data, ZBX_PROTO_TAG_DATA);

	for (i = 0; i < disks.values_num; i++)
	{
		zbx_json_addobject(&json_data, NULL);
		zbx_json_addstring(&json_data, "{#FSNAME}", disks.values[i], ZBX_JSON_TYPE_STRING);
		zbx_json_close(&json_data);
	}

	zbx_json_close(&json_data);

	zbx_vector_str_clean(&disks);
	zbx_vector_str_destroy(&disks);

	SET_STR_RESULT(result, zbx_strdup(NULL, json_data.buffer));

	zbx_json_free(&json_data);

	ret = SYSINFO_RET_OK;
unlock:
	zbx_vmware_unlock();

	return ret;
}

int	check_vcenter_vm_vfs_fs_size(AGENT_REQUEST *request, const char *username, const char *password,
		AGENT_RESULT *result)
{
	zbx_vmware_service_t	*service;
	zbx_vmware_vm_t		*vm;
	char			*url, *uuid, *fsname, *mode, *value = NULL, xpath[MAX_STRING_LEN];
	zbx_uint64_t		value_total, value_free;
	int			ret = SYSINFO_RET_FAIL;

	if (3 > request->nparam || request->nparam > 4)
		return SYSINFO_RET_FAIL;

	url = get_rparam(request, 0);
	uuid = get_rparam(request, 1);
	fsname = get_rparam(request, 2);
	mode = get_rparam(request, 3);

	if ('\0' == *uuid)
		return SYSINFO_RET_FAIL;

	zbx_vmware_lock();

	if (NULL == (service = get_vmware_service(url, username, password, result, &ret)))
		goto unlock;

	if (NULL == (vm = service_vm_get(service, uuid)))
		goto unlock;

	zbx_snprintf(xpath, sizeof(xpath),
			ZBX_XPATH_LN2("disk", "diskPath") "[.='%s']/.." ZBX_XPATH_LN("capacity"), fsname);

	if (NULL == (value = zbx_xml_read_value(vm->details, xpath)))
		goto unlock;

	if (SUCCEED != is_uint64(value, &value_total))
		goto unlock;

	zbx_free(value);

	zbx_snprintf(xpath, sizeof(xpath),
			ZBX_XPATH_LN2("disk", "diskPath") "[.='%s']/.." ZBX_XPATH_LN("freeSpace"), fsname);

	if (NULL == (value = zbx_xml_read_value(vm->details, xpath)))
		goto unlock;

	if (SUCCEED != is_uint64(value, &value_free))
		goto unlock;

	ret = SYSINFO_RET_OK;

	if (NULL == mode || '\0' == *mode || 0 == strcmp(mode, "total"))
		SET_UI64_RESULT(result, value_total);
	else if (0 == strcmp(mode, "free"))
		SET_UI64_RESULT(result, value_free);
	else if (0 == strcmp(mode, "used"))
		SET_UI64_RESULT(result, value_total - value_free);
	else if (0 == strcmp(mode, "pfree"))
		SET_DBL_RESULT(result, (0 != value_total ? (double)(100.0 * value_free) / value_total : 0));
	else if (0 == strcmp(mode, "pused"))
		SET_DBL_RESULT(result, 100.0 - (0 != value_total ? (double)(100.0 * value_free) / value_total : 0));
	else
		ret = SYSINFO_RET_FAIL;
unlock:
	zbx_free(value);

	zbx_vmware_unlock();

	return ret;
}

#endif	/* defined(HAVE_LIBXML2) && defined(HAVE_LIBCURL) */
