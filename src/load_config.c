#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libxml/parser.h>
#include <libxml/xpath.h>

#include "perf_constants.h"
#include "load_config.h"

#define CONFIG_FILE_ENV_VAR "LTTNG_PERF_SAMPLING_CONFIG"
#define CONFIG_FILENAME_DEFAULT "sampling_config.xml"

#define ROOT_TYPE_NAME "sampling_config"

#define SIGNO_XPATH "/" ROOT_TYPE_NAME "/@signal"
#define DEBUG_XPATH "/" ROOT_TYPE_NAME "/@debug"
#define ERROR_STREAM_XPATH "/" ROOT_TYPE_NAME "/@error_stream"
#define EVENT_XPATH "/" ROOT_TYPE_NAME "/event"
#define EVENT_TYPE_XPATH "@type"
#define EVENT_CONFIG_XPATH "@config"
#define EVENT_PERIOD_XPATH "@period"

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
	config->debug = 0;
	config->events = NULL;

	struct perf_event* def_event = malloc(sizeof(struct perf_event));

	perf_event_init(def_event);

	def_event->attr.type = PERF_TYPE_HARDWARE;
	def_event->attr.config = PERF_COUNT_HW_CACHE_MISSES;

	perf_config_add_event(config, def_event);
}

int load_config_from_file(struct perf_sampling_config* config, char* filename)
{
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
	context = xmlXPathNewContext(doc);
	ERROR_GOTO(context == NULL,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_DOC,
		   "Error creating XPath context");

	// -> Get signal number
	result = xmlXPathEvalExpression(SIGNO_XPATH, context);
	ERROR_GOTO(result == NULL,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_CONTEXT,
		   "Error evaluating XPath expression");
	if(xmlXPathNodeSetIsEmpty(result->nodesetval)) {
		config->signo = 0;  // Set to 0, which will set the
				    // default signal
	} else {
		// This will default to 0 if malformed, which will set our
		// default signal
		config->signo =
			(int) xmlXPathCastNodeSetToNumber(result->nodesetval);
	}
	xmlXPathFreeObject(result);

	// -> Debug enabled?
	result = xmlXPathEvalExpression(DEBUG_XPATH, context);
	ERROR_GOTO(result == NULL,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_CONTEXT,
		   "Error evaluating XPath expression");
	if(xmlXPathNodeSetIsEmpty(result->nodesetval)) {
		config->debug = 0;  // Disable debugging information by default
	} else {
		config->debug =
			(int) xmlXPathCastNodeSetToBoolean(result->nodesetval);
	}
	xmlXPathFreeObject(result);

	// -> Get the error stream to use
	xmlChar * error_stream, * tofree;
	result = xmlXPathEvalExpression(ERROR_STREAM_XPATH, context);
	ERROR_GOTO(result == NULL,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_CONTEXT,
		   "Error evaluating XPath expression");
	error_stream = tofree = xmlXPathCastNodeSetToString(result->nodesetval);
	if(xmlXPathNodeSetIsEmpty(result->nodesetval)) {
		config->error_stream_fd = STDERR_FILENO;
	} else {
		config->error_stream_fd = -1;
		if (!xmlStrcmp(error_stream, (const xmlChar *) "stderr")) {
			config->error_stream_fd = STDERR_FILENO;
		} else if (!xmlStrcmp(error_stream, (const xmlChar *) "stdout")) {
			config->error_stream_fd = STDOUT_FILENO;
		} else if (!xmlStrcmp(error_stream, (const xmlChar *) "lttng-logger")) {
			error_stream = "/proc/lttng-logger";
		}
		// Open the error stream as a file
		config->error_stream_fd = open(error_stream, O_RDWR);
		ERROR_GOTO(config->error_stream_fd < 0,
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_ERROR_STREAM,
			   "Error opening file error stream");
	}
	xmlXPathFreeObject(result);

	// -> Load the perf_event descriptions
	result = xmlXPathEvalExpression(EVENT_XPATH, context);
	ERROR_GOTO(result == NULL,
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_CONTEXT,
		   "Error evaluating XPath expression");
	ERROR_GOTO(xmlXPathNodeSetIsEmpty(result->nodesetval),
		   LOAD_CONFIG_FROM_FILE_ERROR_FREE_XPATH,
		   "Could not find perf_event descriptions in config file %s",
		   filename);
	struct perf_event* new_event;
	xmlNodePtr event_node;
	xmlXPathObjectPtr event_result;
	config->events = NULL;
	for(int i = 0; i < result->nodesetval->nodeNr; ++i) {
		new_event = malloc(sizeof(struct perf_event));
		perf_event_init(new_event);

		event_node = result->nodesetval->nodeTab[i];

		// Event type
		xmlChar* event_type;
		event_result = xmlXPathNodeEval(event_node, EVENT_TYPE_XPATH, context);
		ERROR_GOTO(event_result == NULL,
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_XPATH,
			   "Error evaluating XPath expression");
		ERROR_GOTO(xmlXPathNodeSetIsEmpty(event_result->nodesetval),
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_EVENT_RESULT,
			   "Event is missing its type attribute");
		event_type = xmlXPathCastNodeSetToString(event_result->nodesetval);
		new_event->attr.type = perf_get_constant_value(event_type);
		xmlFree(event_type);
		ERROR_GOTO(new_event->attr.type == -1,
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_EVENT_RESULT,
			   "Event type string is invalid");
		xmlXPathFreeObject(event_result);

		// Event config
		xmlChar* event_config;
		event_result = xmlXPathNodeEval(event_node, EVENT_CONFIG_XPATH, context);
		ERROR_GOTO(event_result == NULL,
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_XPATH,
			   "Error evaluating XPath expression");
		ERROR_GOTO(xmlXPathNodeSetIsEmpty(event_result->nodesetval),
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_EVENT_RESULT,
			   "Event is missing its event configuration");
		event_config = xmlXPathCastNodeSetToString(event_result->nodesetval);
		new_event->attr.config = perf_get_constant_value(event_config);
		xmlFree(event_config);
		ERROR_GOTO(new_event->attr.config == -1,
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_EVENT_RESULT,
			   "Event config string is invalid");
		xmlXPathFreeObject(event_result);

		// Event period
		int event_period;
		event_result = xmlXPathNodeEval(event_node, EVENT_PERIOD_XPATH, context);
		ERROR_GOTO(event_result == NULL,
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_XPATH,
			   "Error evaluating XPath expression");
		ERROR_GOTO(xmlXPathNodeSetIsEmpty(event_result->nodesetval),
			   LOAD_CONFIG_FROM_FILE_ERROR_FREE_EVENT_RESULT,
			   "Event is missing its period attribute");
		event_period = xmlXPathCastNodeSetToNumber(event_result->nodesetval);
		new_event->attr.sample_period = event_period;

		// Add event to config
		perf_config_add_event(config, new_event);
	}

	retval = 0;
	// Free what is still allocated
LOAD_CONFIG_FROM_FILE_ERROR_FREE_EVENT_RESULT:
	xmlXPathFreeObject(event_result);
LOAD_CONFIG_FROM_FILE_ERROR_FREE_ERROR_STREAM:
	xmlFree(tofree);
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
