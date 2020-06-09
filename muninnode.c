/*
 *  Munin node for lwIP-1.4.1/2.1.2
 * 
 *  muninnode.c
 *         
 *  Copyright (c) 2017,2019 Bastiaan van Kesteren <bas@edeation.nl>
 *  This program comes with ABSOLUTELY NO WARRANTY; for details see the file LICENSE.
 *  This program is free software; you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 */
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include <string.h>
#include "muninnode.h"

//#define MUNINNODEDEBUG
#define MUNINNODE_PORT      4949
#define MUNINNODE_VERSION   "munin lwIP node version: 0.1\n"
#define MUNINNODE_WELCOME   "# munin node at metering\n"
#define MUNINNODE_NODES     "metering\n.\n"

#define MUNINNODE_MAXPLUGINNAMELENGTH   10

struct tcp_pcb* muninnode_pcb;
const pluginentry_t* muninnode_plugins;

extern void muninplugins_init(void);

typedef enum {
    MUNINNODE_IDLE,
    MUNINNODE_ACCEPTED,
    MUNINNODE_RECEIVED,
    MUNINNODE_CLOSING
} muninnode_state_t;

typedef struct {
    struct tcp_pcb* pcb;
    struct pbuf* buf;
    char pendingcmd[50];
    unsigned char pendingcmd_ptr;
    char pendingoutput[10];
    unsigned char pendingoutput_ptr;
    muninnode_state_t state;
} muninnodectrl_t;

muninnodectrl_t muninnode;

static void muninnode_write(muninnodectrl_t *node, char * data)
/*

*/
{
    err_t result;
    unsigned short length;
    
    length = strlen(data);
    do {
        result = tcp_write(node->pcb, data, length, TCP_WRITE_FLAG_COPY);

        if(result == ERR_MEM) {
            if((tcp_sndbuf(node->pcb) == 0) || (tcp_sndqueuelen(node->pcb) >= TCP_SND_QUEUELEN)) {
                /* No need to try smaller sizes */
                break;
            }
            else {
                length /= 2;
            }
        }
    } while (result == ERR_MEM && length);
    
    #ifdef MUNINNODEDEBUG
    if(result != ERR_OK) {
        dprint("muninnode_write() failed (%s)\n\r", lwip_strerr(result));
    }
    #endif
}

static void muninnode_close(muninnodectrl_t *node)
/*
 
*/
{
    #ifdef MUNINNODEDEBUG
    dprint("muninnode_close()\n\r");
    #endif
    
    if(node->pcb != NULL) {
        tcp_arg(node->pcb, NULL);
        tcp_sent(node->pcb, NULL);
        tcp_recv(node->pcb, NULL);
        tcp_err(node->pcb, NULL);
        tcp_poll(node->pcb, NULL, 0);
        tcp_close(node->pcb);
    }
  
    node->state = MUNINNODE_IDLE;
}

static void muninnode_send(muninnodectrl_t *node, struct tcp_pcb *tpcb)
/*

*/
{
    struct pbuf *ptr;
    err_t wr_err = ERR_OK;
 
    while ((wr_err == ERR_OK) && (node->buf != NULL) && (node->buf->len <= tcp_sndbuf(tpcb))) {
        ptr = node->buf;
        
        wr_err = tcp_write(tpcb, ptr->payload, ptr->len, 1);
        if(wr_err == ERR_OK) {
            u16_t plen;
            u8_t freed;

            plen = ptr->len;
            /* continue with next pbuf in chain (if any) */
            node->buf = ptr->next;
            if(node->buf != NULL) {
                /* new reference! */
                pbuf_ref(node->buf);
            }
            /* chop first pbuf from chain */
            do {
                /* try hard to free pbuf */
                freed = pbuf_free(ptr);
            } while(freed == 0);
            /* we can read more data now */
            tcp_recved(tpcb, plen);
        }
        else if(wr_err == ERR_MEM) {
            /* we are low on memory, try later / harder, defer to poll */
            node->buf = ptr;
        }
        #ifdef MUNINNODEDEBUG
        else {
            /* other problem ?? */
            dprint("muninnode_send() error %d, now what?\n\r", wr_err);)
        }
        #endif
    }
}

static err_t muninnode_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
/*
  Handle data connection acknowledge of sent data
*/
{
    muninnodectrl_t *node = (muninnodectrl_t*)arg;
    
    if(node->buf != NULL) {
        /* Got more to send */
        tcp_sent(tpcb, muninnode_sent);
        muninnode_send(node, tpcb);
    }
    else {
        /* Nothing left to send */
        if(node->state == MUNINNODE_CLOSING) {
            muninnode_close(node);
        }
    }
    return ERR_OK;
}

static err_t muninnode_poll(void *arg, struct tcp_pcb *tpcb)
/*

*/
{
    muninnodectrl_t *node = (muninnodectrl_t*)arg;
    
    if(node != NULL) {
        if(node->buf != NULL) {
            /* there is a remaining pbuf (chain)  */
            tcp_sent(tpcb, muninnode_sent);
            muninnode_send(node, tpcb);
        }
        else {
            /* no remaining pbuf (chain)  */
            if(node->state == MUNINNODE_CLOSING) {
                muninnode_close(node);
            }   
        }
    }
    else {
        tcp_abort(tpcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static void muninnode_err(void *arg, err_t err)
/*

*/
{
    #ifdef MUNINNODEDEBUG
    dprint("muninnode_err() %d\n\r", err);
    #endif
        
    if (arg != NULL) {
        muninnodectrl_t *node = (muninnodectrl_t*)arg;

        node->state = MUNINNODE_IDLE;
    }
}

static void muninnode_recv_parser(muninnodectrl_t *node)
/*

*/
{
    unsigned char i=0;
    unsigned char j;
    
    #ifdef MUNINNODEDEBUG
    dprint("muninnode_recv_parser() [%s]\n\r", node->pendingcmd);
    #endif

    if(strcmp(node->pendingcmd, "cap") == 0 || strncmp(node->pendingcmd, "cap ", 4) == 0) {
        muninnode_write(node, "cap\n");
    }
    else if(strcmp(node->pendingcmd, "list") == 0 || strncmp(node->pendingcmd, "list ", 5) == 0) {
        // TODO: if a node-name is given, we assume it's us...
        i=0;
        do {
            if(i) {
                muninnode_write(node, " ");
            }
            muninnode_write(node, muninnode_plugins[i].name);
            i++;
        } while(muninnode_plugins[i].name != NULL);
        muninnode_write(node, "\n");
    }
    else if(strcmp(node->pendingcmd, "nodes") == 0) {
        muninnode_write(node, MUNINNODE_NODES);
    }
    else if(strcmp(node->pendingcmd, "config") == 0 || strncmp(node->pendingcmd, "config ", 7) == 0) {
        for(j=7;j < strlen(node->pendingcmd);j++) {
            if(node->pendingcmd[j] != ' ') {
                do {
                    if(strcmp(muninnode_plugins[i].name, &node->pendingcmd[j]) == 0) {
                        muninnode_plugins[i].config(node);
                        muninnode_write(node, ".\n");
                        return;
                    }
                    i++;
                } while(muninnode_plugins[i].name != NULL);
                muninnode_write(node, "# unknown plugin ");
                muninnode_write(node, &node->pendingcmd[j]);
                muninnode_write(node, "\n");
                return;
            }
        }
        
        muninnode_write(node, "# no plugin given\n");
    }
    else if(strcmp(node->pendingcmd, "fetch") == 0 || strncmp(node->pendingcmd, "fetch ", 6) == 0) {
        for(j=5;j < strlen(node->pendingcmd);j++) {
            if(node->pendingcmd[j] != ' ') {
                do {
                    if(strcmp(muninnode_plugins[i].name, &node->pendingcmd[j]) == 0) {
                        muninnode_plugins[i].values(node);
                        muninnode_write(node, ".\n");
                        return;
                    }
                    i++;
                } while(muninnode_plugins[i].name != NULL);
                muninnode_write(node, "# unknown plugin ");
                muninnode_write(node, &node->pendingcmd[j]);
                muninnode_write(node, "\n");
                return;
            }
        }
        
        muninnode_write(node, "# no plugin given\n");
    }
    else if(strcmp(node->pendingcmd, "version") == 0) {
        muninnode_write(node, MUNINNODE_VERSION);
    }
    else if(strcmp(node->pendingcmd, "quit") == 0) {
        muninnode_close(node);
    }
    else {
        #ifdef MUNINNODEDEBUG
        dprint("muninnode_recv_parser() unknown\n\r");
        #endif
        muninnode_write(node, "# unknown command. Try cap, list, nodes, config, fetch, version or quit\n");
    }
}

static err_t muninnode_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
/*
  Handle incoming data
*/
{
    muninnodectrl_t *node = (muninnodectrl_t*)arg;
    struct pbuf *q;
    unsigned short i;

    if (err == ERR_OK) {
        if (p) {
            for (q=p; q != NULL; q=q->next) {
                /* Figure out what it is */
                for(i=0;i<q->len;i++) {
                    if(((char *)q->payload)[i] == '\n') {
                        /* End-of-command, close string, trim trailing spaces, call parser */
                        do {
                            node->pendingcmd[node->pendingcmd_ptr] = '\0';
                            node->pendingcmd_ptr--;
                        } while (node->pendingcmd[node->pendingcmd_ptr] == 0x20 && node->pendingcmd_ptr);
                        node->pendingcmd_ptr = 0;
                        muninnode_recv_parser(node);
                    }
                    else if(node->pendingcmd_ptr < sizeof(node->pendingcmd)) {
                        /* Add to pendingcmd if readable character */
                        if(((char *)q->payload)[i] >= 32 && ((char *)q->payload)[i] <= 126) {
                            node->pendingcmd[node->pendingcmd_ptr] = ((char *)q->payload)[i];
                            node->pendingcmd_ptr++;
                        }
                        #ifdef MUNINNODEDEBUG
                        else {
                            dprint("muninnode_recv() skipping char 0x%x\n\r", ((char *)q->payload)[i]);
                        }
                        #endif
                    }
                    else {
                        /* Won't fit in buffer, restart */
                        node->pendingcmd_ptr = 0;
                        
                        #ifdef MUNINNODEDEBUG
                        dprint("muninnode_recv() pendingcmd overflow\n\r");
                        #endif
                    }
                }
            }
            tcp_recved(tpcb, p->tot_len);
            pbuf_free(p);
        }
        else {
            #ifdef MUNINNODEDEBUG
            dprint("muninnode_recv() client closed connection\n\r");
            #endif
            muninnode_close(node);
        }
    }
    else {
        #ifdef MUNINNODEDEBUG
        dprint("muninnode_recv() reception failure (%s)\n\r",lwip_strerr(err));
        #endif
        muninnode_close(node);
    }

    return err;
}

static err_t muninnode_accept(void *arg, struct tcp_pcb *pcb, err_t err)
/*
  Handle incoming connections
*/
{   
    if(err != ERR_OK || pcb == NULL) {
        return ERR_VAL;
    }

    if(muninnode.state != MUNINNODE_IDLE) {
        #ifdef MUNINNODEDEBUG
        dprint("muninnode_accept() in use (state %d)\n\r", muninnode.state);
        #endif
        tcp_close(pcb);
    }
    else {
        #ifdef MUNINNODEDEBUG
        dprint("muninnode_accept() OK\n\r");
        #endif
        
        muninnode.pcb = pcb;
        muninnode.state = MUNINNODE_ACCEPTED;
        muninnode.pendingcmd_ptr = 0;
        muninnode.pendingoutput_ptr = 0;
        
        /* Setup handler argument (each handler gets the 'muninnode' state thingy) */
        tcp_arg(muninnode.pcb, &muninnode);
    
        /* Setup our handlers */
        tcp_recv(muninnode.pcb, muninnode_recv);
        tcp_err(muninnode.pcb, muninnode_err);
        tcp_poll(muninnode.pcb, muninnode_poll, 0);
        tcp_sent(muninnode.pcb, muninnode_sent);
        
        muninnode_write(&muninnode, MUNINNODE_WELCOME);
    }
    
    return ERR_OK;
}

err_t muninnode_init(const pluginentry_t *plugins)
/*
  Start Munin node
*/
{
    err_t err;
    
    muninplugins_init();

    /* Create Protocol Control Block */
    if((muninnode_pcb = tcp_new()) == NULL) {
        /* Out-of-memory? */
        #ifdef MUNINNODEDEBUG
        dprint("muninnode_init() tcp_new failed\n\r");
        #endif
        muninnode_pcb = NULL;
        return ERR_MEM;
    }
    
    /* Bind to port */
    err = tcp_bind(muninnode_pcb, IP_ADDR_ANY, MUNINNODE_PORT);
    if(err != ERR_OK) {
        #ifdef MUNINNODEDEBUG
        dprint("muninnode_init() tcp_bind failed (%d)\n\r", err);
        #endif
        tcp_close(muninnode_pcb);
        muninnode_pcb = NULL;
        return err;
    }

    /* Make our PCB a listener */
    muninnode_pcb = tcp_listen(muninnode_pcb);
    if(muninnode_pcb == NULL) {
        #ifdef MUNINNODEDEBUG
        dprint("muninnode_init() tcp_listen failed\n\r");
        #endif
        muninnode_pcb = NULL;
        return ERR_MEM;
    }
    tcp_accept(muninnode_pcb, muninnode_accept);
    
    muninnode.state = MUNINNODE_IDLE;
    muninnode_plugins = plugins;
    
    return ERR_OK;
}

void muninnode_putchar(void *client, const unsigned char c)
/*

*/
{
    muninnodectrl_t *node = (muninnodectrl_t*)client;
    
    /* Add to pendingoutput if readable character */
    if(c >= 32 && c <= 126) {
        node->pendingoutput[node->pendingoutput_ptr] = c;
        node->pendingoutput_ptr++;
    }
    
    /* Buffer full? */
    if(node->pendingoutput_ptr + 1 == sizeof(node->pendingoutput)) {
        node->pendingoutput[node->pendingoutput_ptr] = '\0';
        muninnode_write(node, node->pendingoutput);
        node->pendingoutput_ptr = 0;
    }
    /* Newline? */
    else if(c == '\n') {
        if(node->pendingoutput_ptr) {
            node->pendingoutput[node->pendingoutput_ptr] = '\0';
            muninnode_write(node, node->pendingoutput);
            node->pendingoutput_ptr = 0;
        }
        muninnode_write(node, "\n");
    }
}
