
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <map>
#include <cctype>

enum ParseState {
    START,
    TRANSITION,
    BOOL,
    NUMBER,
    STRING,
    ARRAY,
    OBJECT_KEY,
    OBJECT_VAL,
};

struct JsonVal {
    enum Type {
        NONE,  // NULL
        BOOL,
        NUMBER,
        STRING,
        ARRAY,
        OBJECT,
    };

    Type type;

    union {
        bool boolean;
        float number;
        std::string* string;
        std::vector<JsonVal*>* array;
        std::map<std::string, JsonVal*>* object;
    };
    

    void print_json(int level) {
        for (int i=0; i < level; i++) {
            std::cout << "  ";
        }

        switch (type) {
            case NONE:
                std::cout << "null" << std::endl;
                break;
            case STRING:
                std::cout << "\"" << *string << "\"" << std::endl;
                break;
            case OBJECT:
                std::cout << "{" << std::endl;
                for (const auto& pair : *object) {
                    for (int i=0; i < level+1; i++) {
                        std::cout << "  ";
                    }
                    std::cout << "\"" << pair.first << "\": ";
                    pair.second->print_json(level + 1);
                }
                for (int i=0; i < level; i++) {
                    std::cout << "  ";
                }
                std::cout << "}" << std::endl;
                break;
            default:
                break;
        }
    }
};

int parse_json(int fd) {
    
    std::vector<JsonVal*> stack;
    JsonVal* root = new JsonVal();
    root->type = JsonVal::OBJECT;
    root->object = new std::map<std::string, JsonVal*>;
    stack.push_back(root);
    JsonVal* current = root;

    char c;
    ParseState state = START;
    std::string token;

    while (read(fd, &c, 1) > 0) {
        switch (state) {
            case START:
                if ('{' == c) {
                    state = OBJECT_KEY;
                } else if (std::isspace(c)) {
                    break;
                }
                break;
            
            case OBJECT_KEY:
                if (std::isspace(c) && token.empty()) {
                    break;
                }
                if ('"' == c && token.empty()) {
                    token += c;
                    break;
                }
                
                if ('"' == c && !token.empty()) {
                    token = token.substr(1);  // remove starting quote
                    (*current->object)[token] = nullptr;
                    token.clear();
                    state = OBJECT_VAL;
                    break;
                }

                token += c;
                break;

            case OBJECT_VAL:
                if (std::isspace(c) && token.empty()) {
                    break;
                }
                if ('"' == c) {
                    state = STRING;
                } else if ('{' == c) {
                    JsonVal* new_obj = new JsonVal();
                    new_obj->type = JsonVal::OBJECT;
                    new_obj->object = new std::map<std::string, JsonVal*>;
                    
                    std::string key = current->object->rbegin()->first;
                    (*current->object)[key] = new_obj;
                    
                    stack.push_back(new_obj);
                    current = new_obj;
                    state = OBJECT_KEY;
                }
                break;

            case STRING:
                if ('"' == c) {
                    std::string key = current->object->rbegin()->first;
                    
                    JsonVal* string_val = new JsonVal();
                    string_val->type = JsonVal::STRING;
                    string_val->string = new std::string(token);
                    
                    (*current->object)[key] = string_val;
                    
                    token.clear();
                    state = TRANSITION;
                    break;
                }
                token += c;
                break;

            case TRANSITION:
                if (',' == c) {
                    state = OBJECT_KEY;
                } else if ('}' == c) {
                    if (stack.size() > 1) {
                        stack.pop_back();
                        current = stack.back();
                    }
                    state = TRANSITION;
                } else if (std::isspace(c)) {
                    break;
                }
                break;

            default:
                break;
        }
    }

    std::cout << "JSON Output:" << std::endl;
    root->print_json(0);

    return 0;
}