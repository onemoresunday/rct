#include "Value.h"

#include "../cJSON/cJSON.h"

void Value::clear()
{
    switch (mType) {
    case Type_String:
        stringPtr()->~String();
        break;
    case Type_Map:
        mapPtr()->~Map<String, Value>();
        break;
    case Type_List:
        listPtr()->~List<Value>();
        break;
    case Type_Custom:
        customPtr()->~shared_ptr<Custom>();
        break;
    default:
        break;
    }

    mType = Type_Invalid;
}

void Value::copy(const Value &other)
{
    assert(isNull());
    mType = other.mType;
    switch (mType) {
    case Type_String:
        new (mData.stringBuf) String(*other.stringPtr());
        break;
    case Type_Map:
        new (mData.mapBuf) Map<String, Value>(*other.mapPtr());
        break;
    case Type_List:
        new (mData.listBuf) List<Value>(*other.listPtr());
        break;
    case Type_Custom:
        new (mData.customBuf) std::shared_ptr<Custom>(*other.customPtr());
        break;
    default:
        memcpy(&mData, &other.mData, sizeof(mData));
        break;
    }
}

static Value fromCJSON(const cJSON *object)
{
    assert(object);
    switch (object->type) {
    case cJSON_False:
        return Value(false);
    case cJSON_True:
        return Value(true);
    case cJSON_NULL:
        break;
    case cJSON_Number:
        if (object->valueint == object->valuedouble)
            return Value(object->valueint);
        return Value(object->valuedouble);
    case cJSON_String:
        return Value(String(object->valuestring));
    case cJSON_Array: {
        List<Value> values;
        for (const cJSON *child = object->child; child; child = child->next) {
            values.append(fromCJSON(child));
        }
        return values; }
    case cJSON_Object: {
        Map<String, Value> values;
        for (const cJSON *child = object->child; child; child = child->next)
            values[child->string] = fromCJSON(child);
        return values; }
    }
    return Value();
}

Value Value::fromJSON(const char *json, bool *ok)
{
    cJSON *obj = cJSON_Parse(json);
    if (!obj) {
        if (ok)
            *ok = false;
        return Value();
    }

    const Value ret = ::fromCJSON(obj);
    if (ok)
        *ok = true;
    cJSON_Delete(obj);
    return ret;
}

cJSON *Value::toCJSON(const Value &value)
{
    switch (value.type()) {
    case Value::Type_Boolean: return value.toBool() ? cJSON_CreateTrue() : cJSON_CreateFalse();
    case Value::Type_Date:
    case Value::Type_Integer: return cJSON_CreateNumber(value.toInteger());
    case Value::Type_Double: return cJSON_CreateNumber(value.toDouble());
    case Value::Type_String: return cJSON_CreateString(value.toString().constData());
    case Value::Type_List: {
        cJSON *array = cJSON_CreateArray();
        for (const auto &v : *value.listPtr())
            cJSON_AddItemToArray(array, toCJSON(v));
        return array; }
    case Value::Type_Map: {
        cJSON *object = cJSON_CreateObject();
        for (const auto &v : *value.mapPtr())
            cJSON_AddItemToObject(object, v.first.constData(), v.second.toCJSON(v.second));
        return object; }
    case Value::Type_Invalid:
        break;
    case Value::Type_Undefined:
        break;
    case Value::Type_Custom:
        if (std::shared_ptr<Value::Custom> custom = value.toCustom()) {
            cJSON *ret = cJSON_CreateString(custom->toString().constData());
            if (ret) {
                ret->type = cJSON_RawString;
                return ret;
            }
        }
        break;
    }
    return cJSON_CreateNull();
}

class JSONFormatter : public Value::Formatter
{
public:
    virtual void format(const Value &value, std::function<void(const char *, size_t)> output) const
    {
        size_t i;
        auto escape = [&output, &i](const String &str) {
            output("\"", 1);
            bool hasEscaped = false;
            auto put = [&output, &hasEscaped, &i, &str](const char *escaped) {
                if (!hasEscaped) {
                    hasEscaped = true;
                    if (i)
                        output(str.constData(), i);
                }
                output(escaped, strlen(escaped));
            };
            const char *stringData = str.constData();

            const size_t length = str.size();
            for (i = 0; i < length; ++i) {
                switch (const char ch = stringData[i]) {
                case 8: put("\\b"); break; // backspace
                case 12: put("\\f"); break; // Form feed
                case '\n': put("\\n"); break; // newline
                case '\t': put("\\t"); break; // tab
                case '\r': put("\\r"); break; // carriage return
                case '"': put("\\\""); break; // quote
                case '\\': put("\\\\"); break; // backslash
                default:
                    if (ch < 0x20 || ch == 127) { // escape non printable characters
                        char buffer[7];
                        snprintf(buffer, 7, "\\u%04x", ch);
                        put(buffer);
                        break;
                    } else if (hasEscaped) {
                        output(&ch, 1);
                    }
                    break;
                }
            }

            if (!hasEscaped)
                output(stringData, length);
            output("\"", 1);
        };
        switch (value.type()) {
        case Value::Type_Invalid:
        case Value::Type_Undefined:
            output("null", 4);
            break;
        case Value::Type_Boolean:
            if (value.toBool()) {
                output("true", 4);
            } else {
                output("false", 5);
            }
            break;
        case Value::Type_Integer: {
            char buf[128];
            const size_t w = snprintf(buf, sizeof(buf), "%d", value.toInteger());
            output(buf, w);
            break; }
        case Value::Type_Double: {
            char buf[128];
            const size_t w = snprintf(buf, sizeof(buf), "%g", value.toDouble());
            output(buf, w);
            break; }
        case Value::Type_String:
            escape(value.toString());
            break;
        case Value::Type_Custom:
            escape(value.toCustom()->toString());
            break;
        case Value::Type_Map: {
            const auto end = value.end();
            bool first = true;
            output("{", 1);
            for (auto it = value.begin(); it != end; ++it) {
                if (!first) {
                    output(",", 1);
                } else {
                    first = false;
                }
                escape(it->first);
                output(":", 1);
                format(it->second, output);
            }
            output("}", 1);
            break; }
        case Value::Type_List: {
            const auto end = value.listEnd();
            output("[", 1);
            bool first = true;
            for (auto it = value.listBegin(); it != end; ++it) {
                if (!first) {
                    output(",", 1);
                } else {
                    first = false;
                }
                format(*it, output);
            }
            output("]", 1);
            break; }
        case Value::Type_Date:
            escape(String::formatTime(value.toDate().time()));
            break;
        }
    }
};

String Value::toJSON(bool pretty) const
{
    cJSON *json = toCJSON(*this);
    char *formatted = (pretty ? cJSON_Print(json) : cJSON_PrintUnformatted(json));
    cJSON_Delete(json);
    const String ret = formatted;
    free(formatted);
    return ret;
}

class StringFormatter : public Value::Formatter
{
public:
    mutable int indent = 0;
    virtual void format(const Value &value, std::function<void(const char *, size_t)> output) const
    {
        String str;
        switch (value.type()) {
        case Value::Type_Invalid:
        case Value::Type_Undefined:
            output("null", 4);
            break;
        case Value::Type_Boolean:
            if (value.toBool()) {
                output("true", 4);
            } else {
                output("false", 5);
            }
            break;
        case Value::Type_Integer: {
            char buf[128];
            const size_t w = snprintf(buf, sizeof(buf), "%d", value.toInteger());
            output(buf, w);
            break; }
        case Value::Type_Double: {
            char buf[128];
            const size_t w = snprintf(buf, sizeof(buf), "%g", value.toDouble());
            output(buf, w);
            break; }
        case Value::Type_String:
            str = value.toString();
            break;
        case Value::Type_Custom:
            str = value.toCustom()->toString();
            break;
        case Value::Type_Map: {
            const auto end = value.end();
            List<String> strings;
            ++indent;
            for (auto it = value.begin(); it != end; ++it) {
                // printf("%*s" "%s", indent, " ", string);
                String str = String::format<128>("%*s%s: ", indent - 1, " ", it->first.constData());
                format(it->second, [&str](const char *ch, size_t len) {
                        str.append(ch, len);
                    });
                output(str.constData(), str.size());
                output("\n", 1);
            }
            --indent;
            break; }
        case Value::Type_List: {
            const auto end = value.listEnd();
            output("[ ", 1);
            bool first = true;
            for (auto it = value.listBegin(); it != end; ++it) {
                if (!first) {
                    output(", ", 1);
                } else {
                    first = false;
                }
                format(*it, output);
            }
            output(" ]", 2);
            break; }
        case Value::Type_Date:
            str = String::formatTime(value.toDate().time());
            break;
        }
        if (!str.isEmpty())
            output(str.constData(), str.size());
    }
};

String Value::format() const
{
    return StringFormatter().toString(*this);
}
