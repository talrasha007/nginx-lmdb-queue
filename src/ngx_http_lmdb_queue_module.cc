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
		  NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE2,
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
		return NGX_CONF_OK;
	}
	
	static char *ngx_http_lmdb_queue_topic(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
		return NGX_CONF_OK;
	}
	
	static char *ngx_http_lmdb_queue_push(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
		return NGX_CONF_OK;
	}
}