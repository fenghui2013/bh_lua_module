#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <pthread.h>

#include "bh_lua_module.h"

typedef struct bh_lua_vm bh_lua_vm;
struct bh_lua_vm {
    lua_State *vm;
    int vm_id;
    pthread_mutex_t vm_lock;
};

typedef struct bh_lua_state bh_lua_state;
struct bh_lua_state {
    int vm_id;
    lua_State *state;
    int sock_fd;
    bh_lua_state *prev;
    bh_lua_state *next;
};

struct bh_lua_module {
    bh_lua_vm *lua_vm;
    int current_vm;
    int vm_count;
    bh_lua_state *first;
    bh_lua_state *last;
    pthread_mutex_t lua_state_lock;
};

bh_lua_module *
bh_lua_module_create(int vm_count) {
    int i, res;
    bh_lua_module *lua_module = (bh_lua_module *)malloc(sizeof(bh_lua_module));
    lua_module->first = NULL;
    lua_module->last = NULL;
    res = pthread_mutex_init(&(lua_module->lua_state_lock), NULL);
    if (res != 0) {
        printf("pthread_mutex_init failed\n");
        exit(0);
    }
    lua_module->lua_vm = (bh_lua_vm *)malloc(vm_count*sizeof(bh_lua_vm));

    for (i=0; i<vm_count; i++) {
        lua_module->lua_vm[i]->vm = luaL_newstate();
        luaL_openlibs(lua_module->lua_vm[i]->vm);
        lua_module->lua_vm[i]->vm_id = i;
        res = pthread_mutex_init(&(lua_module->lua_vm[i]->vm_lock), NULL);
        if (res != 0) {
            printf("pthread_mutex_init failed\n");
            exit(0);
        }
    }
    lua_module->current_vm = 0;
    lua_module->vm_count = vm_count;

    return lua_module;
}

void
bh_lua_module_load(bh_lua_module *lua_module, const char *mod_name) {
    int i, res;

    for (i=0; i<lua_module->vm_count; i++) {
        luaL_loadfile(lua_module->lua_vm[i]->vm, mod_name);
        res = lua_pcall(lua_module->lua_vm[i]->vm, 0, LUA_MULTRET, 0);
        printf("load module: %s, res: %d\n", mod_name, res);
    }
}

static int
_test(lua_State *L, int sock_fd, char *data, int len) {
    lua_getglobal(L, "test");
    lua_pushinteger(L, sock_fd);
    lua_pushlstring(L, data, len);
    lua_pushinteger(L, len);
    
    return lua_resume(L, NULL, 3);
}

void
bh_lua_module_test(bh_lua_module *lua_module, int sock_fd, char *data, int len) {
    int res;
    bh_lua_state *temp_lua_state = NULL;
    lua_State *L = NULL;

    pthread_mutex_lock(&(lua_module->lua_state_lock));
    temp_lua_state = _find(lua_module->first, sock_fd);
    if (temp_lua_state == NULL) {
        L = lua_newthread(lua_module->lua_vm[lua_module->current_vm]->vm);
        temp_lua_state = (bh_lua_state *)malloc(sizeof(bh_lua_state));
        temp_lua_state->state = L;
        temp_lua_state->sock_fd = sock_fd;
        temp_lua_state->vm_id = lua_module->current_vm;
        temp_lua_state->prev = NULL;
        temp_lua_state->next = NULL;
        if (lua_module->first == NULL) {
            lua_module->first = temp_lua_state;
            lua_module->last = temp_lua_state;
        } else {
            lua_module->last->next = temp_lua_state;
            temp_lua_state->prev = lua_module->last;
            lua_module->last = temp_lua_state;
        }
        lua_module->current_vm += 1;
        if (lua_module->current_vm == lua_module->vm_count) {
            lua_module->current_vm = 0;
        }
    }
    L = temp_lua_state->state;
    pthread_mutex_unlock(&(lua_module->lua_state_lock));

    pthread_mutex_lock(&(lua_module->lua_vm[temp_lua_state->vm_id]->vm_lock));
    res = _test(L, sock_fd, data, len);
    pthread_mutex_unlock(&(lua_module->lua_vm[temp_lua_state->vm_id]->vm_lock));

    pthread_mutex_lock(&(lua_module->lua_state_lock));
    if (res == LUA_OK) {
        if (temp_lua_state->prev==NULL && temp_lua_state->next==NULL) {
            lua_module->first = NULL;
            lua_module->last = NULL;
        } else if (temp_lua_state->prev==NULL && temp_lua_state->next!=NULL) {
            lua_module->first = temp_lua_state->next;
            temp_lua_state->next->prev = NULL;
        } else if (temp_lua_state->prev!=NULL && temp_lua_state->next==NULL) {
            lua_module->last = temp_lua_state->prev;
            temp_lua_state->prev->next = NULL;
        } else {
            temp_lua_state->prev->next = temp_lua_state->next;
            temp_lua_state->next->prev = temp_lua_state->prev;
        }
        free(temp_lua_state);
        temp_lua_state = NULL;
    } else if (res == LUA_YIELD) {
    } else {
        printf("lua_error: %d\n", res);
    }
    pthread_mutex_unlock(&(lua_module->lua_state_lock));

}

void
bh_lua_module_release(bh_lua_module *lua_module) {
    int i;

    for (i=0; i<lua_module->vm_count; i++) {
        lua_close(lua_module->lua_vm[i]->vm);
        pthread_mutex_destroy(&(lua_module->lua_vm[i]->vm_lock));
    }
    pthread_mutex_destroy(&(lua_module->lua_state_lock));
    free(lua_module->lua_vm);
    free(lua_module);
    lua_module = NULL;
}
