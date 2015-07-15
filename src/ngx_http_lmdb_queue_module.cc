#include <algorithm>
#include <vector>
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
		Producer* producer;
		
		size_t data_format_len;
		u_char data_format[1024 * 8];
		
		size_t vars_count;
		ngx_int_t* vars;		
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
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V %s", &args[1], strerror(errno));
			return (char*)NGX_CONF_ERROR;
		}
	}
	
	static char *ngx_http_lmdb_queue_topic(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
		ngx_str_t *args = (ngx_str_t*)cf->args->elts;
		const char *name = (const char*)args[1].data;
		
		const char *chunkSizeStr = (const char*)args[2].data;
		size_t chunkSize = strtoull(chunkSizeStr, NULL, 10);
		if (chunkSize == ULLONG_MAX) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V %s", &args[2], strerror(errno));
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
				ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V: Invalid topic chunk size unit(should be m|g).", &args[2]);
				return (char*)NGX_CONF_ERROR;
		}
		
		if (chunkSize < 64 * 1024 * 1024 || chunkSize > size_t(64) * 1024 * 1024 * 1024) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V: Invalid topic chunk size unit(should between 64MB and 64GB).", &args[2]);
			return (char*)NGX_CONF_ERROR;
		}
		
		size_t chunksToKeep = strtoull((const char*)args[3].data, NULL, 10);
		if (chunksToKeep == ULLONG_MAX) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V: %s", &args[2], strerror(errno));
			return (char*)NGX_CONF_ERROR;			
		}
		if (chunksToKeep < 4) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "%V: You should keep at least 4 chunks.", &args[3]);
			return (char*)NGX_CONF_ERROR;
		}
		
		//printf("Args: %s %zu %zu", name, chunkSize, chunksToKeep);
		auto &ptr = producers[name];
		if (ptr.get() == NULL) {
			TopicOpt qopt = { chunkSize, chunksToKeep };
			ptr.reset(new Producer(queue_path, name, &qopt));
		}
		
		return NGX_CONF_OK;
	}
	
	static char *ngx_http_lmdb_queue_push(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
		ngx_str_t *args = (ngx_str_t*)cf->args->elts;

		const char *topic = (const char*)args[1].data;
		auto producerIter = producers.find(topic);
		
		if (producerIter == producers.end()) {
			ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "Topic '%V' not exists.", &args[1]);
			return (char*)NGX_CONF_ERROR;
		}
		
		ngx_http_lmdb_queue_loc_conf *locconf = (ngx_http_lmdb_queue_loc_conf*)conf;
		
		std::vector<ngx_int_t> varIndexes;
		u_char *end = args[2].data + args[2].len, *varCur = NULL, *outCur = locconf->data_format;
		for (u_char *s = args[2].data; s < end;) {
			if (*s == '$') {
				varCur = ++s;
				*outCur++ = '\1';
				while (s < end && ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_')) {
					++s;
				}

				ngx_str_t varName { size_t(s - varCur), varCur };
				ngx_int_t idx = ngx_http_get_variable_index(cf, &varName);
				if (idx == NGX_ERROR) {
					return (char*)NGX_CONF_ERROR;
				}
				
				varIndexes.push_back(idx);				
			}
			
			if (*s != '$' && s < end) {
				*outCur++ = *s++;
			}
		}
		
		locconf->data_format_len = size_t(outCur - locconf->data_format);
		locconf->producer = producerIter->second.get();
		locconf->vars_count = varIndexes.size();
		locconf->vars = (ngx_int_t*)ngx_pcalloc(cf->pool, varIndexes.size() * sizeof(ngx_int_t));
		std::copy(varIndexes.begin(), varIndexes.end(), locconf->vars);

		return NGX_CONF_OK;
	}

	static ngx_int_t ngx_http_lmdb_queue_handler_init(ngx_conf_t *cf) {
		ngx_http_core_main_conf_t *cmcf = (ngx_http_core_main_conf_t*)ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
		ngx_http_handler_pt *h = (ngx_http_handler_pt*)ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
		if (h == NULL) {
			return NGX_ERROR;
		}

		*h = ngx_http_lmdb_queue_handler;
		return NGX_OK;	
	}
	
	static ngx_int_t ngx_http_lmdb_queue_handler(ngx_http_request_t *r) {
		ngx_http_lmdb_queue_loc_conf *lcf = (ngx_http_lmdb_queue_loc_conf*)ngx_http_get_module_loc_conf(r, ngx_http_accesskey_module);
		if (lcf->producer == NULL) {
			return NGX_OK;
		}
		
		std::vector<ngx_http_variable_value_t*> vals;
		size_t resLen = lcf->data_format_len;
		for (int i = 0; i < lcf->vars_count; ++i) {
			ngx_http_variable_value_t *v = ngx_http_get_indexed_variable(r, lcf->vars[i]);
			vals.push_back(v);
			
			if (v && !v->not_found) {
				resLen += (v->len - 1);
			} else {
				--resLen;
			}
		}
		
		u_char *formatCur = lcf->data_format, *formatEnd = lcf->data_format + lcf->data_format_len; 
		u_char *buf = new u_char[resLen], *cur = buf;
		auto valIter = vals.begin();
		while (formatCur < formatEnd) {
			if (*formatCur != '\1') {
				*cur++ = *formatCur++;
			} else {
				++formatCur;
				ngx_http_variable_value_t *v = *valIter++;
				if (v && !v->not_found) {
					for (u_char *src = v->data; src < v->data + v->len;) {
						*cur++ = *src++;
					}
				}
			}
		}
		
		Producer::BatchType bt;
		bt.push_back(make_tuple((char*)buf, resLen));
		lcf->producer.push(bt);

		return NGX_OK;
	}
}