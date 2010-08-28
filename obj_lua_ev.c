#include <stdarg.h>

static char* obj_registry = "ev{obj}";

/**
 * Create a "registry" of light userdata pointers into the
 * fulluserdata so that we can get handles into the lua objects.
 */
static void create_obj_registry(lua_State *L) {
    lua_pushlightuserdata(L, &obj_registry);
    lua_newtable(L);

    lua_createtable(L,  0, 1);
    lua_pushstring(L,   "v");
    lua_setfield(L,     -2, "__mode");
    lua_setmetatable(L, -2);

    lua_rawset(L, LUA_REGISTRYINDEX);
}

/**
 * Count the number of registered objects.  This exists only so we can
 * validate that objects are properly GC'ed.
 *
 * [-0, +1, e]
 */
static int obj_count(lua_State *L) {
    int count = 0;

    lua_pushlightuserdata(L, &obj_registry);
    lua_rawget(L,            LUA_REGISTRYINDEX);
    assert(lua_istable(L, -1) /* create_obj_registry() should have ran */);

    lua_pushnil(L);
    while ( lua_next(L, -2) != 0 ) {
        count++;
        lua_pop(L, 1);
    }
    lua_pushinteger(L, count);
    return 1;
}

/**
 * Create a new "object" with a metatable of tname and allocate size
 * bytes for the object.
 *
 * [-0, +1, ?]
 */
static void* obj_new(lua_State* L, size_t size, const char* tname) {
    void* obj;

    /*
     * class = luaL_getmetatable(tname)
     * shadow = setmetatable({ }, class)
     * fenv = { __gc = class.__gc, __index = shadow, __newindex = shadow }
     * setfenv(obj, fenv)
     * setmetatable(obj, fenv)
     *
     * to check:  getmetatable(getmetatable(obj).__index) == class
     */

    obj = lua_newuserdata(L, size);    /* obj */
    lua_newtable(L);                   /* obj shadow */
    lua_newtable(L);                   /* obj shadow fenv */
    luaL_getmetatable(L, tname);       /* obj shadow fenv class */

    assert(lua_istable(L, -1) /* tname was loaded */);

    /* fenv.__gc = class.__gc */
    lua_pushliteral(L, "__gc");        /* obj shadow fenv class "__gc" */
    lua_rawget(L, -2);                 /* obj shadow fenv class __gc */
    lua_setfield(L, -3, "__gc");       /* obj shadow fenv class */
    /* setmetatable(shadow, class) */
    lua_setmetatable(L, -3);           /* obj shadow fenv */
    /* fenv.__index = shadow */
    lua_pushvalue(L, -2);              /* obj shadow fenv shadow */
    lua_setfield(L, -2, "__index");    /* obj shadow fenv */
    /* fenv.__newindex = shadow */
    lua_pushvalue(L, -2);              /* obj shadow fenv shadow */
    lua_setfield(L, -2, "__newindex"); /* obj shadow fenv */
    lua_remove(L, -2);                 /* obj fenv */
    /* setfenv(obj, fenv) */
    lua_pushvalue(L, -1);              /* obj fenv fenv */
    lua_setfenv(L, -3);                /* obj fenv */
    /* setmetatable(obj, fenv) */
    lua_setmetatable(L, -2);           /* obj */

    return obj;
}

/**
 * Checks that "object" has a metatable of tname.
 *
 * [-0, +0, ?]
 */
static void *obj_check(lua_State *L, int obj_i, const char *tname) {
    void *udata = lua_touserdata(L, obj_i);
    if (udata && lua_getmetatable(L, obj_i)) {
        lua_pushliteral(L, "__index");
        lua_rawget(L, -2);
        if (lua_getmetatable(L, -1)) {
            luaL_getmetatable(L, tname);
            if (lua_rawequal(L, -1, -2)) {
                lua_pop(L, 4);
                return udata;
            }
        }
    }
    luaL_typerror(L, obj_i, tname);
    return NULL; /* not reached */
}

/**
 * Register the lua object at index obj_i so it is keyed off of the
 * obj pointer.
 *
 * [-0, +0, ?]
 */
static void register_obj(lua_State*L, int obj_i, void* obj) {
    obj_i = abs_index(L, obj_i);

    lua_pushlightuserdata(L, &obj_registry);
    lua_rawget(L,            LUA_REGISTRYINDEX);
    assert(lua_istable(L, -1) /* create_obj_registry() should have ran */);

    lua_pushlightuserdata(L, obj);
    lua_pushvalue(L,         obj_i);
    lua_rawset(L,            -3);
    lua_pop(L,               1);
}

/**
 * Simply calls push_objs() with a single object.
 */
static int push_obj(lua_State* L, void* obj) {
    void* objs[2] = { obj, NULL };
    return push_objs(L, objs);
}

/**
 * Pushes the lua representation of n objects onto the stack.  The
 * objs array must be NULL terminated.  Returns the number of objects
 * pushed onto the stack.
 *
 * [-0, +objs_len, m]
 */
static int push_objs(lua_State* L, void** objs) {
    int obj_count = 0;
    int registry_i;
    void** cur;

    for ( cur=objs; *cur; ++cur ) obj_count++;

    if ( 0 == obj_count ) return obj_count;

    lua_checkstack(L, obj_count + 1);

    lua_pushlightuserdata(L, &obj_registry);
    lua_rawget(L,            LUA_REGISTRYINDEX);
    assert(lua_istable(L, -1) /* create_obj_registry() should have ran */);

    registry_i = lua_gettop(L);
    for ( cur=objs; *cur; ++cur ) {
        lua_pushlightuserdata(L, *cur);
        lua_rawget(L, registry_i);
    }

    lua_remove(L, registry_i);

    return obj_count;
}
