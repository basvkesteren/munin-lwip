# munin-lwip
Munin node for lwIP-1.4.1/2.1.2

## Current status
'works for me'; I use this in a house-metering setup, running stable for more than a year now.
It's not a full munin-node implementation, but as long as the munin-server behaves and you have only one munin-server (why would you have more than one?), everything should work.

## Usage
Create a struct of type 'pluginentry_t' (as typedef'd in muninnode.h) to define your plugins. Format of each struct-entry is "name", "config-function", value-function". Here's what I use:

```
const static pluginentry_t munin_plugins[] = {
    {"netspan", munin_mains_config, munin_mains_values},
    {"elek_act", munin_elektra_actual_config, munin_elektra_actual_values},
    {"elektra", munin_elektra_config, munin_elektra_values},
    {"gas", munin_gas_config, munin_gas_values},
    {"gas_act", munin_gas_actual_config, munin_gas_actual_values},
    {"water", munin_water_config, munin_water_values},
    {"water_act", munin_water_actual_config, munin_water_actual_values},
    {"cv", munin_cv_config, munin_cv_values},
    {"accu", munin_accu_config, munin_accu_values},
    {"xbeetemp", munin_xbee_temperatures_config, munin_xbee_temperatures_values},
    
    /* End-of-list */
    {NULL, NULL, NULL}
};
```

Then define a muninplugins_init() function (will be called by muninnode_init()), and a muninplugins_putchar(), to be used by the plugin-functions:

```
void *client_inuse;

void muninplugins_init()
{
    client_inuse = NULL;
}

static void muninplugins_putchar(unsigned char c)
{
    muninnode_putchar(client_inuse, c);
}
```

The 'config-function' is called when the munin-server needs the configuratien of the plugin. For example:

```
bool munin_mains_config(void* client)
{
    if(client_inuse == NULL) {
        client_inuse = client;
        
        fprint(muninplugins_putchar, "graph_title Netspanning\n");
        fprint(muninplugins_putchar, "netspanning.label Spanning, V AC\n");
        fprint(muninplugins_putchar, "frequentie.label Frequentie, Hz\n");
        
        client_inuse = NULL;
        
        return TRUE;
    }
    return FALSE;
}
```

Likewise the 'values-function' is called when the munin-server needs values (note that the 'muninplugins_printvalue' used in this example just calls muninplugins_putchar depending on it's parameters):

```
bool munin_mains_values(void* client)
{
    extern sunspec_info_t sunspecinfo;
    
    if(client_inuse == NULL) {
        client_inuse = client;
        
        muninplugins_printvalue("netspanning", sunspecinfo.ACVoltage, sunspecinfo.uptodate);
        muninplugins_printvalue_decimal("frequentie", sunspecinfo.ACFrequency/100, sunspecinfo.ACFrequency%100, sunspecinfo.uptodate);
        
        client_inuse = NULL;
        
        return TRUE;
    }
    return FALSE;
}
```

Finally, call muninnode_init() to start the node:

```
if(muninnode_init(munin_plugins) != ERR_OK) {
    logging("muninnode_init failed", TRUE);
}
```
