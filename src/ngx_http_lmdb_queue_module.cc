#include <string>
#include <memory>
#include <map>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include "producer.h"

std::string queue_path;
std::map<std::string, std::unique_ptr<Producer> > producers;

extern "C" {
	#include <ngx_config.h>
	#include <ngx_core.h>
	#include <ngx_http.h>

	static void *ngx_http_lmdb_queue_create_loc_conf(ngx_conf_t *cf);
	
	/* Directive handlers */
	static char *ngx_http_lmdb_queue(ngx_conf_t *cf, ngx_command_t *cmd, void *conf); // Declare lmdb_queue
	static char *ngx_http_lmdb_queue_topic(ngx_conf_t *cf, ngx_command_t *cmd, void *conf); // Declare lmdb_queue topic
	static char *ngx_http_lmdb_queue_push(ngx_conf_t *cf, ngx_command_t *cmd, void *conf); // Declare lmdb_queue topic

	/* Filter */
	ngx_http_output_body_filter_pt ngx_http_next_body_filter;
	static ngx_int_t ngx_http_lmdb_queue_handler_init(ngx_conf_t *cf);
	static ngx_int_t ngx_http_lmdb_queue_handler(ngx_http_request_t *r);

	/* Directives */
	static ngx_command_t ngx_http_lmdb_queue_commands[] = {
		{ ngx_string("lmdb_queue"),
		  NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
		  ngx_http_lmdb_queue,
		  NGX_HTTP_MAIN_CONF_OFFSET,
		  0,
		  NULL },
		{ ngx_string("lmdb_queue_topic"),
		  NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE3,
		  ngx_http_lmdb_queue_topic,
		  NGX_HTTP_MAIN_CONF_OFFSET,
		  0,
		  NULL },
		{ ngx_string("lmdb_queue_push"),
		  NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE2,
		  ngx_http_lmdb_queue_push,
		  NGX_HTTP_LOC_CONF_OFFSET,
		  0,
		  NULL },
		ngx_null_command
	};

	static ngx_http_module_t ngx_http_lmdb_queue_module_ctx = {
		NULL, /* preconfiguration */
		ngx_http_lmdb_queue_handler_init, /* postconfiguration */
		
		NULL, /* create main configuration */
		NULL, /* init main configuration */
		
		NULL, /* create server configuration */
		NULL, /* merge server configuration */
		
		ngx_http_lmdb_queue_create_loc_conf, /* create location configuration */
		NULL  /* merge location configuration */
	};

	ngx_module_t ngx_http_lmdb_queue_module = {
		NGX_MODULE_V1,
		&ngx_http_lmdb_queue_module_ctx,       /* module context */
		ngx_http_lmdb_queue_commands,          /* module directives */
		NGX_HTTP_MODULE,                       /* module type */
		NULL,                                  /* init master */
		NULL,                                  /* init module */
		NULL,                                  /* init process */
		NULL,                                  /* init thread */
		NULL,                                  /* exit thread */
		NULL,                                  /* exit process */
		NULL,                                  /* exit master */
		NGX_MODULE_V1_PADDING
	};
	
	struct ngx_http_lmdb_queue_loc_conf {
		char data_type[64];
		Producer* producer;
	};
	
	static void *ngx_http_lmdb_queue_create_loc_conf(ngx_conf_t *cf) {
		ngx_http_lmdb_queue_loc_conf *conf = (ngx_http_lmdb_queue_loc_conf*)ngx_pcalloc(cf->pool, sizeof(ngx_http_lmdb_queue_loc_conf));
		if (conf == NULL) {
			return NGX_CONF_ERROR;
		}
		
		ngx_memzero(conf, sizeof(ngx_http_lmdb_queue_loc_conf));
		return conf;
	}

	static char *ngx_http_lmdb_queue(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
		ngx_str_t *args = (ngx_str_t*)cf->args->elts;
		const char *path = (const char*)args[1].data;
		int res = mkdir(path, 0766);
		if (res == 0 || errno == EEXIST) {
			queue_path = path;
			return NGX_CONF_OK;
		} else {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, strerror(errno), args[1]);
			return (char*)NGX_CONF_ERROR;
		}
	}
	
	static char *ngx_http_lmdb_queue_topic(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
		ngx_str_t *args = (ngx_str_t*)cf->args->elts;
		const char *name = (const char*)args[1].data;
		
		const char *chunkSizeStr = (const char*)args[2].data;
		size_t chunkSize = strtoull(chunkSizeStr, NULL, 10);
		if (chunkSize == ULLONG_MAX) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, strerror(errno), args[2]);
			return (char*)NGX_CONF_ERROR;			
		}
		
		char unit = chunkSizeStr[args[2].len - 1];
		switch (unit) {
			case 'm':
			case 'M':
				chunkSize *= 1024 * 1024;
				break;
			case 'g':
			case 'G':
				chunkSize *= 1024 * 1024 * 1024;
				break;
			default:
				ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Invalid topic chunk size unit(should be m|g).", args[2]);
				return (char*)NGX_CONF_ERROR;
		}
		
		if (chunkSize < 64 * 1024 * 1024 || chunkSize > size_t(64) * 1024 * 1024 * 1024) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Invalid topic chunk size unit(should between 64MB and 64GB).", args[2]);
			return (char*)NGX_CONF_ERROR;
		}
		
		size_t chunksToKeep = strtoull((const char*)args[3].data, NULL, 10);
		if (chunksToKeep == ULLONG_MAX) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, strerror(errno), args[2]);
			return (char*)NGX_CONF_ERROR;			
		}
		if (chunksToKeep < 4) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "You should keep at least 4 chunks.", args[2]);
			return (char*)NGX_CONF_ERROR;
		}
		
		//printf("Args: %s %zu %zu", name, chunkSize, chunksToKeep);
		auto &ptr = producers[name];
		if (ptr.get()) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Topic already decalred.", args[2]);
			return (char*)NGX_CONF_ERROR;
		}
		
		TopicOpt qopt = { chunkSize, chunksToKeep };
		ptr.reset(new Producer(queue_path, name, &qopt));
		
		return NGX_CONF_OK;
	}
	
	static char *ngx_http_lmdb_queue_push(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
		ngx_str_t *args = (ngx_str_t*)cf->args->elts;

		const char *topic = (const char*)args[1].data;
		const char *type = (const char*)args[2].data;
		auto producerIter = producers.find(topic);
		
		if (producerIter == producers.end()) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Topic not exists.", args[1]);
			return (char*)NGX_CONF_ERROR;
		}
		
		if (strcmp(type, "headers") != 0 && strcmp(type, "headers_and_reqbody") != 0) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Queue data type error(headers|headers_and_reqbody).", args[2]);
			return (char*)NGX_CONF_ERROR;
		}
		
		ngx_http_lmdb_queue_loc_conf *locconf = (ngx_http_lmdb_queue_loc_conf*)conf;
		strcpy(locconf->data_type, type);
		locconf->producer = producerIter->second.get();

		return NGX_CONF_OK;
	}

	static ngx_int_t ngx_http_lmdb_queue_handler_init(ngx_conf_t *cf) {
		ngx_http_core_main_conf_t *cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
		ngx_http_handler_pt *h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
		if (h == NULL) {
			return NGX_ERROR;
		}

		*h = ngx_http_lmdb_queue_handler;
		return NGX_OK;	
	}
	
	static ngx_int_t ngx_http_lmdb_queue_handler(ngx_http_request_t *r) {
		return NGX_OK;
	}
}