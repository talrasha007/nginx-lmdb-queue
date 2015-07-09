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
	
	/* Directive handlers */
	static char *ngx_http_lmdb_queue(ngx_conf_t *cf, ngx_command_t *cmd, void *conf); // Declare lmdb_queue
	static char *ngx_http_lmdb_queue_topic(ngx_conf_t *cf, ngx_command_t *cmd, void *conf); // Declare lmdb_queue topic
	static char *ngx_http_lmdb_queue_push(ngx_conf_t *cf, ngx_command_t *cmd, void *conf); // Declare lmdb_queue topic
	
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
		  NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
		  ngx_http_lmdb_queue_push,
		  NGX_HTTP_LOC_CONF_OFFSET,
		  0,
		  NULL },
		ngx_null_command
	};

	static ngx_http_module_t ngx_http_lmdb_queue_module_ctx = {
		NULL, /* preconfiguration */
		NULL, /* postconfiguration */
		
		NULL, /* create main configuration */
		NULL, /* init main configuration */
		
		NULL, /* create server configuration */
		NULL, /* merge server configuration */
		
		NULL, /* create location configuration */
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
		return NGX_CONF_OK;
	}
}