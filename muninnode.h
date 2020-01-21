/*
 *  Munin node for lwIP-1.4.1/2.1.2
 * 
 *  muninnode.h
 *         
 *  Copyright (c) 2017,2019 Bastiaan van Kesteren <bas@edeation.nl>
 *  This program comes with ABSOLUTELY NO WARRANTY; for details see the file LICENSE.
 *  This program is free software; you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 */
#ifndef __MUNINNODE_H__
#define __MUNINNODE_H__

#include <lwip/tcpip.h>

typedef struct {
    char *name;
    bool (* config)(void* client);
    bool (* values)(void* client);
} pluginentry_t;

err_t muninnode_init(const pluginentry_t *plugins);
void muninnode_putchar(void *client, const unsigned char c);

#endif /* __MUNINNODE_H__ */
