#define remove_var(name) xos_set_var_val(name,NULL,-1,0,os_VARTYPE_STRING,NULL,NULL)
#define set_var_val(name,value) xos_set_var_val(name, (byte *)value, strlen(value), 0, 4, NULL, NULL)

void cgiscript_start(struct connection *conn);
void cgiscript_setvars(struct connection *conn);
void cgiscript_removevars(void);
