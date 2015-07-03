#ifndef _BH_LUA_MODULE_H_
#define _BH_LUA_MODULE_H_

typedef struct bh_lua_module bh_lua_module;

bh_lua_module * bh_lua_module_create(int vm_count);
void            bh_lua_module_load(bh_lua_module *lua_module, const char *mod_name);
void            bh_lua_module_test(bh_lua_module *lua_module, int sock_fd, char *data, int len);
void            bh_lua_module_release(bh_lua_module *lua_module);

#endif
