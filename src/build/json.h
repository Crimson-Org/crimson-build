#pragma once

#include <glib.h>

/* Kinds of value a JSON document keeps */
typedef enum {
	JSON_NULL,
	JSON_BOOLEAN,
	JSON_NUMBER,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT,
} JsonKind;

/* One parsed or built JSON value, where objects keep their members in written order */
typedef struct JsonValue JsonValue;
struct JsonValue {
	JsonKind kind;
	gboolean boolean;
	double number;
	char *string;
	/* Array items, or object member values matching the names at the same index */
	GPtrArray *items;
	GPtrArray *names;
	/* Where a parsed value starts and ends in the text, as byte offsets, and where each member name's opening
	 * quote is, so an editor can change one value without rewriting the rest */
	gsize start, end;
	GArray *name_starts;
};

/* Comments and trailing commas are accepted, since people write task files by hand */
JsonValue *parse_json(const char *text, gssize length, GError **error);
char *write_json(const JsonValue *value);
/* One member or item per line, each level further in by indent, such as a tab or four spaces */
char *write_json_indented(const JsonValue *value, const char *indent);
void free_json(JsonValue *value);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(JsonValue, free_json)

JsonValue *new_json_null(void);
JsonValue *new_json_boolean(gboolean boolean);
JsonValue *new_json_number(double number);
JsonValue *new_json_string(const char *string);
JsonValue *new_json_array(void);
JsonValue *new_json_object(void);
/* Both take ownership of the added value and return the container so calls can be chained */
JsonValue *add_json_item(JsonValue *array, JsonValue *item);
JsonValue *set_json_member(JsonValue *object, const char *name, JsonValue *value);

JsonValue *get_json_member(const JsonValue *object, const char *name);
/* Follows a dotted path of member names, such as "range.start.line" */
JsonValue *find_json_path(const JsonValue *value, const char *path);
const char *get_json_string(const JsonValue *value, const char *fallback);
double get_json_number(const JsonValue *value, double fallback);
gboolean get_json_boolean(const JsonValue *value, gboolean fallback);
guint get_json_length(const JsonValue *value);
JsonValue *get_json_item(const JsonValue *value, guint index);
