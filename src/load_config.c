#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libxml/parser.h>
#include <libxml/xpath.h>

#include "load_config.h"

#define CONFIG_FILE_ENV_VAR "LTTNG_PERF_SAMPLING_CONFIG"
#define CONFIG_FILENAME_DEFAULT "sampling_config.xml"

#define ROOT_TYPE_NAME "sampling_config"

#define SIGNO_XPATH "/" ROOT_TYPE_NAME "/@signal"
#define ERROR_STREAM_XPATH "/" ROOT_TYPE_NAME "/@error_stream"
#define EVENT_ELEM_XPATH "/" ROOT_TYPE_NAME "/element"
#define EVENT_ELEM_TYPE_XPATH "TODO xpath expression"
#define EVENT_ELEM_CONFIG_XPATH "TODO xpath expression"
#define EVENT_ELEM_PERIOD_XPATH "TODO xpath expression"

#define ERROR_STREAM stderr

#define ERROR_RET(err, message, args...) {				\
		if (err) {						\
			fprintf(ERROR_STREAM, message "\n", ##args);	\
			return -1;					\
		}							\
	}

#define ERROR_GOTO(err, label, message, args...) {			\
		if (err) {						\
			fprintf(ERROR_STREAM, message "\n", ##args);	\
			goto label;					\
		}							\
	}

void set_default_config(struct perf_sampling_config* config)
{
	config->signo = 0;
	config->error_stream_fd = STDERR_FILENO;
	config->events = NULL;

	struct perf_event* def_event = malloc(sizeof(struct perf_event));

	perf_event_init(def_event);

	def_event->attr.type = PERF_TYPE_HARDWARE;
	def_event->attr.config = PERF_COUNT_HW_CACHE_MISSES;

	perf_config_add_event(config, def_event);
}


static
int load_config_from_file(struct perf_sampling_config* config, char* filename)
{
	// DEBUG TODO Remove this line. Only to set default events as
	// they are not yet loaded from the config file
	set_default_config(config);

	int retval = -1;
	int err;
	xmlDocPtr doc;
	xmlNodePtr root;

	// Parse the file, and check the root element
	doc = xmlParseFile(filename);
	ERROR_RET(doc == NULL, "Error parsing file %s", filename);

	root = xmlDocGetRootElement(doc);
	ERROR_GOTO(root == NULL,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_DOC,
		   "Error looking for root element: "
		   "Document %s is empty",
		   filename);

	err = xmlStrcmp(root->name, (const xmlChar *) ROOT_TYPE_NAME);
	ERROR_GOTO(err,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_DOC,
		   "Root element of wrong type. Should be "
		   ROOT_TYPE_NAME);

	// Fill successive config values from file with the help of xpath
	xmlXPathContextPtr context;
	xmlXPathObjectPtr result;
	xmlNodeSetPtr nodeset;
	context = xmlXPathNewContext(doc);
	ERROR_GOTO(context == NULL,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_DOC,
		   "Error creating XPath context");

	// -> Get signal number
	result = xmlXPathEvalExpression(SIGNO_XPATH, context);
	ERROR_GOTO(result == NULL,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_CONTEXT,
		   "Error evaluating XPath expression");
	ERROR_GOTO(xmlXPathNodeSetIsEmpty(result->nodesetval),
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_XPATH,
		   "Could not find signal number in config file %s",
		   filename);
	// This will default to 0 if malformed, which will set our
	// default signal
	config->signo =
	  (int) xmlXPathCastNodeSetToNumber(result->nodesetval);
	xmlXPathFreeObject(result);

	// -> Get the error stream to use
	xmlChar * error_stream;
	result = xmlXPathEvalExpression(ERROR_STREAM_XPATH, context);
	ERROR_GOTO(result == NULL,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_CONTEXT,
		   "Error evaluating XPath expression");
	ERROR_GOTO(xmlXPathNodeSetIsEmpty(result->nodesetval),
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_XPATH,
		   "Could not find error stream option in config file %s",
		   filename);
	error_stream = xmlXPathCastNodeSetToString(result->nodesetval);
	xmlXPathFreeObject(result);

	config->error_stream_fd = -1;
	if (xmlStrcmp(error_stream, (const xmlChar *) "stderr")) {
		config->error_stream_fd = STDERR_FILENO;
	} else if (xmlStrcmp(error_stream, (const xmlChar *) "stdout")) {
		config->error_stream_fd = STDOUT_FILENO;
	}
	if (config->error_stream_fd < 0) {
		xmlChar * tofree = error_stream;
		if (xmlStrcmp(error_stream, (const xmlChar *) "lttng-logger")) {
			error_stream = "/proc/lttng-logger";
		}
		// Open the error stream as a file
		config->error_stream_fd = open(error_stream, O_RDWR);
		xmlFree(tofree);
		ERROR_GOTO(config->error_stream_fd < 0,
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_CONTEXT,
			   "Error opening file error stream");
	} else {
		xmlFree(error_stream);
	}

	// TODO load the events (Set to default values above in the mean time)

	retval = 0;
	// Free what is still allocated
LOAD_CONFIG_FROM_FILE_ERROR_FREE_XPATH:
	xmlXPathFreeObject(result);
LOAD_CONFIG_FROM_FILE_ERROR_FREE_CONTEXT:
	xmlXPathFreeContext(context);
LOAD_CONFIG_FROM_FILE_ERROR_FREE_DOC:
	xmlFreeDoc(doc);
	xmlCleanupParser();
	return retval;
}

int load_config(struct perf_sampling_config* config)
{
	int err;

	// TODO set callback from trace / unwind module

	// Check for config filename in environment variable
	char* given;
	char* filename = given = getenv(CONFIG_FILE_ENV_VAR);
	if (!filename) {
		filename = CONFIG_FILENAME_DEFAULT;
	}

	err = load_config_from_file(config, filename);
	if (!err) {
		return 0;
	}

	fprintf(stderr,
		"Could not open config file %s\n",
		filename);

	if (given) {
		// If a file is given and not readable, we should fail
		// loading a configuration
		return -1;
	}

	// File was not specified or found so we fallback on the
	// default internal config
	fprintf(stderr, "Using default config\n");
	set_default_config(config);
	return 0;
}
