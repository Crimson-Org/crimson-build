#include <math.h>
#include <string.h>

#include "build/json.h"

#define MAXIMUM_DEPTH 512

/* Reading position within the text being parsed */
typedef struct {
	const char *text, *end, *position;
	GError **error;
	int depth;
} Parser;

static JsonValue *parse_value(Parser *parser);


static JsonValue *new_value(JsonKind kind) {
	JsonValue *value = g_new0(JsonValue, 1);
	value->kind = kind;
	return value;
}


JsonValue *new_json_null(void) {
	return new_value(JSON_NULL);
}


JsonValue *new_json_boolean(gboolean boolean) {
	JsonValue *value = new_value(JSON_BOOLEAN);
	value->boolean = boolean;
	return value;
}


JsonValue *new_json_number(double number) {
	JsonValue *value = new_value(JSON_NUMBER);
	value->number = number;
	return value;
}


JsonValue *new_json_string(const char *string) {
	JsonValue *value = new_value(JSON_STRING);
	value->string = g_strdup(string ? string : "");
	return value;
}


JsonValue *new_json_array(void) {
	JsonValue *value = new_value(JSON_ARRAY);
	value->items = g_ptr_array_new_with_free_func((GDestroyNotify)free_json);
	return value;
}


JsonValue *new_json_object(void) {
	JsonValue *value = new_value(JSON_OBJECT);
	value->items = g_ptr_array_new_with_free_func((GDestroyNotify)free_json);
	value->names = g_ptr_array_new_with_free_func(g_free);
	return value;
}


void free_json(JsonValue *value) {
	if (!value)
		return;
	g_free(value->string);
	if (value->items)
		g_ptr_array_unref(value->items);
	if (value->names)
		g_ptr_array_unref(value->names);
	if (value->name_starts)
		g_array_unref(value->name_starts);
	g_free(value);
}


JsonValue *add_json_item(JsonValue *array, JsonValue *item) {
	g_ptr_array_add(array->items, item);
	return array;
}


JsonValue *set_json_member(JsonValue *object, const char *name, JsonValue *value) {
	for (guint index = 0; index < object->names->len; index++) {
		if (strcmp(g_ptr_array_index(object->names, index), name) != 0)
			continue;
		free_json(g_ptr_array_index(object->items, index));
		object->items->pdata[index] = value;
		return object;
	}
	g_ptr_array_add(object->names, g_strdup(name));
	g_ptr_array_add(object->items, value);
	return object;
}


JsonValue *get_json_member(const JsonValue *object, const char *name) {
	if (!object || object->kind != JSON_OBJECT)
		return NULL;
	for (guint index = 0; index < object->names->len; index++)
		if (strcmp(g_ptr_array_index(object->names, index), name) == 0)
			return g_ptr_array_index(object->items, index);
	return NULL;
}


JsonValue *find_json_path(const JsonValue *value, const char *path) {
	g_auto(GStrv) names = g_strsplit(path, ".", -1);
	JsonValue *current = (JsonValue *)value;
	for (int index = 0; names[index] && current; index++)
		current = get_json_member(current, names[index]);
	return current;
}


const char *get_json_string(const JsonValue *value, const char *fallback) {
	return value && value->kind == JSON_STRING ? value->string : fallback;
}


double get_json_number(const JsonValue *value, double fallback) {
	return value && value->kind == JSON_NUMBER ? value->number : fallback;
}


gboolean get_json_boolean(const JsonValue *value, gboolean fallback) {
	return value && value->kind == JSON_BOOLEAN ? value->boolean : fallback;
}


guint get_json_length(const JsonValue *value) {
	return value && value->items ? value->items->len : 0;
}


JsonValue *get_json_item(const JsonValue *value, guint index) {
	return index < get_json_length(value) ? g_ptr_array_index(value->items, index) : NULL;
}


/* ==================================== Parsing ==================================== */


static int get_line_number(Parser *parser) {
	int line = 1;
	for (const char *character = parser->text; character < parser->position; character++)
		line += *character == '\n';
	return line;
}


static void *fail(Parser *parser, const char *problem) {
	if (parser->error && !*parser->error)
		g_set_error(parser->error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE, "%s on line %d", problem,
			get_line_number(parser));
	return NULL;
}


static void skip_space(Parser *parser) {
	while (parser->position < parser->end) {
		char character = *parser->position;
		if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
			parser->position++;
		} else if (character == '/' && parser->position + 1 < parser->end && parser->position[1] == '/') {
			while (parser->position < parser->end && *parser->position != '\n')
				parser->position++;
		} else if (character == '/' && parser->position + 1 < parser->end && parser->position[1] == '*') {
			const char *close = g_strstr_len(parser->position + 2, parser->end - parser->position - 2, "*/");
			parser->position = close ? close + 2 : parser->end;
		} else {
			return;
		}
	}
}


static gboolean read_word(Parser *parser, const char *word) {
	gsize length = strlen(word);
	if ((gsize)(parser->end - parser->position) < length || strncmp(parser->position, word, length) != 0)
		return FALSE;
	parser->position += length;
	return TRUE;
}


static int read_hex_digits(Parser *parser) {
	if (parser->end - parser->position < 4)
		return -1;
	int code = 0;
	for (int index = 0; index < 4; index++) {
		int digit = g_ascii_xdigit_value(parser->position[index]);
		if (digit < 0)
			return -1;
		code = code * 16 + digit;
	}
	parser->position += 4;
	return code;
}


/* Reads a \u escape, joining the two halves of a surrogate pair into one character */
static gboolean read_unicode_escape(Parser *parser, GString *text) {
	int code = read_hex_digits(parser);
	if (code < 0)
		return FALSE;
	if (code >= 0xD800 && code <= 0xDBFF && read_word(parser, "\\u")) {
		int low = read_hex_digits(parser);
		if (low < 0xDC00 || low > 0xDFFF)
			return FALSE;
		code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
	}
	g_string_append_unichar(text, code);
	return TRUE;
}


static char *parse_string_text(Parser *parser) {
	parser->position++;
	g_autoptr(GString) text = g_string_new(NULL);
	while (parser->position < parser->end && *parser->position != '"') {
		const char *run = parser->position;
		while (parser->position < parser->end && *parser->position != '"' && *parser->position != '\\')
			parser->position++;
		g_string_append_len(text, run, parser->position - run);
		if (parser->position >= parser->end || *parser->position == '"')
			break;
		parser->position++;
		if (parser->position >= parser->end)
			break;
		char escape = *parser->position++;
		static const char ESCAPES[] = "\"\"\\\\//b\bf\fn\nr\rt\t";
		const char *found = escape == 'u' || escape == '\0' ? NULL : strchr(ESCAPES, escape);
		if (escape == 'u') {
			if (!read_unicode_escape(parser, text))
				return fail(parser, "Broken \\u escape");
		} else if (found && (found - ESCAPES) % 2 == 0) {
			g_string_append_c(text, found[1]);
		} else {
			return fail(parser, "Unknown escape in a string");
		}
	}
	if (parser->position >= parser->end)
		return fail(parser, "Unfinished string");
	parser->position++;
	return g_string_free(g_steal_pointer(&text), FALSE);
}


static JsonValue *parse_number(Parser *parser) {
	char *number_end = NULL;
	double number = g_ascii_strtod(parser->position, &number_end);
	if (number_end == parser->position || number_end > parser->end)
		return fail(parser, "Unexpected character");
	parser->position = number_end;
	return new_json_number(number);
}


static JsonValue *parse_container(Parser *parser, gboolean is_object) {
	if (++parser->depth > MAXIMUM_DEPTH)
		return fail(parser, "Too deeply nested");
	parser->position++;
	g_autoptr(JsonValue) container = is_object ? new_json_object() : new_json_array();
	char close = is_object ? '}' : ']';
	for (;;) {
		skip_space(parser);
		if (parser->position < parser->end && *parser->position == close)
			break;
		g_autofree char *name = NULL;
		gsize name_start = (gsize)(parser->position - parser->text);
		if (is_object) {
			if (parser->position >= parser->end || *parser->position != '"')
				return fail(parser, "Expected a quoted name");
			if (!(name = parse_string_text(parser)))
				return NULL;
			skip_space(parser);
			if (!read_word(parser, ":"))
				return fail(parser, "Expected a colon after a name");
		}
		JsonValue *item = parse_value(parser);
		if (!item)
			return NULL;
		guint count = container->items->len;
		if (is_object)
			set_json_member(container, name, item);
		else
			add_json_item(container, item);
		/* A repeated name replaces the earlier member, which keeps its place and so its recorded start */
		if (is_object && container->items->len > count) {
			if (!container->name_starts)
				container->name_starts = g_array_new(FALSE, FALSE, sizeof(gsize));
			g_array_append_val(container->name_starts, name_start);
		}
		skip_space(parser);
		if (read_word(parser, ","))
			continue;
		if (parser->position < parser->end && *parser->position == close)
			break;
		return fail(parser, is_object ? "Expected a comma or }" : "Expected a comma or ]");
	}
	parser->position++;
	parser->depth--;
	return g_steal_pointer(&container);
}


static JsonValue *parse_bare_value(Parser *parser);


static JsonValue *parse_value(Parser *parser) {
	skip_space(parser);
	gsize start = (gsize)(parser->position - parser->text);
	JsonValue *value = parse_bare_value(parser);
	if (value) {
		value->start = start;
		value->end = (gsize)(parser->position - parser->text);
	}
	return value;
}


static JsonValue *parse_bare_value(Parser *parser) {
	skip_space(parser);
	if (parser->position >= parser->end)
		return fail(parser, "Unexpected end of text");
	char character = *parser->position;
	if (character == '{' || character == '[')
		return parse_container(parser, character == '{');
	if (character == '"') {
		char *text = parse_string_text(parser);
		if (!text)
			return NULL;
		JsonValue *value = new_value(JSON_STRING);
		value->string = text;
		return value;
	}
	if (read_word(parser, "true"))
		return new_json_boolean(TRUE);
	if (read_word(parser, "false"))
		return new_json_boolean(FALSE);
	if (read_word(parser, "null"))
		return new_json_null();
	return parse_number(parser);
}


JsonValue *parse_json(const char *text, gssize length, GError **error) {
	gsize size = length < 0 ? strlen(text) : (gsize)length;
	Parser parser = {text, text + size, text, error, 0};
	g_autoptr(JsonValue) value = parse_value(&parser);
	if (!value)
		return NULL;
	skip_space(&parser);
	if (parser.position != parser.end)
		return fail(&parser, "Unexpected text after the end");
	return g_steal_pointer(&value);
}


/* ==================================== Writing ==================================== */


static void write_string(GString *output, const char *text) {
	g_string_append_c(output, '"');
	for (const unsigned char *character = (const unsigned char *)text; *character; character++) {
		switch (*character) {
		case '"': g_string_append(output, "\\\""); break;
		case '\\': g_string_append(output, "\\\\"); break;
		case '\n': g_string_append(output, "\\n"); break;
		case '\r': g_string_append(output, "\\r"); break;
		case '\t': g_string_append(output, "\\t"); break;
		default:
			if (*character < 0x20)
				g_string_append_printf(output, "\\u%04x", *character);
			else
				g_string_append_c(output, (char)*character);
		}
	}
	g_string_append_c(output, '"');
}


static void write_line_start(GString *output, const char *indent, int depth) {
	g_string_append_c(output, '\n');
	for (int level = 0; level < depth; level++)
		g_string_append(output, indent);
}


/* Writes compactly when indent is NULL, or one member or item per line otherwise */
static void write_value(GString *output, const JsonValue *value, const char *indent, int depth) {
	switch (value->kind) {
	case JSON_NULL:
		g_string_append(output, "null");
		break;
	case JSON_BOOLEAN:
		g_string_append(output, value->boolean ? "true" : "false");
		break;
	case JSON_NUMBER: {
		char buffer[G_ASCII_DTOSTR_BUF_SIZE];
		/* Whole numbers are written without a fraction, as protocol fields such as ids expect */
		if (value->number == floor(value->number) && fabs(value->number) < 1e15)
			g_string_append_printf(output, "%.0f", value->number);
		else
			g_string_append(output, g_ascii_dtostr(buffer, sizeof buffer, value->number));
		break;
	}
	case JSON_STRING:
		write_string(output, value->string);
		break;
	case JSON_ARRAY:
	case JSON_OBJECT:
		g_string_append_c(output, value->kind == JSON_OBJECT ? '{' : '[');
		for (guint index = 0; index < value->items->len; index++) {
			if (index > 0)
				g_string_append_c(output, ',');
			if (indent)
				write_line_start(output, indent, depth + 1);
			if (value->kind == JSON_OBJECT) {
				write_string(output, g_ptr_array_index(value->names, index));
				g_string_append(output, indent ? ": " : ":");
			}
			write_value(output, g_ptr_array_index(value->items, index), indent, depth + 1);
		}
		if (indent && value->items->len > 0)
			write_line_start(output, indent, depth);
		g_string_append_c(output, value->kind == JSON_OBJECT ? '}' : ']');
		break;
	}
}


char *write_json(const JsonValue *value) {
	GString *output = g_string_new(NULL);
	write_value(output, value, NULL, 0);
	return g_string_free(output, FALSE);
}


char *write_json_indented(const JsonValue *value, const char *indent) {
	GString *output = g_string_new(NULL);
	write_value(output, value, indent, 0);
	return g_string_free(output, FALSE);
}
