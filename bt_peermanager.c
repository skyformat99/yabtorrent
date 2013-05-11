/**
 * @file
 * @brief Major class tasked with managing downloads
 * @author  Willem Thiart himself@willemthiart.com
 * @version 0.1
 *
 * @section LICENSE
 * Copyright (c) 2011, Willem-Hendrik Thiart
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * The names of its contributors may not be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL WILLEM-HENDRIK THIART BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* for uint32_t */
#include <stdint.h>

#include <sys/time.h>

#include <stdarg.h>

#include "pwp_connection.h"

#include "bitfield.h"
#include "event_timer.h"
#include "config.h"

#include "bt.h"
#include "bt_local.h"
#include "bt_block_readwriter_i.h"
#include "bt_peermanager.h"
#include "bt_piece_db.h"
//#include "bt_string.h"

#include "bt_client_private.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <time.h>

static void __log(void *bto, void *src, const char *fmt, ...)
{
    bt_client_t *bt = bto;
    char buf[1024];
    va_list args;

    if (!bt->func_log)
        return;

    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    printf("%s\n", buf);
//    bt->func_log(bt->log_udata, NULL, buf);
}

/*
 * peer connections are given this as a callback whenever they want to send
 * information */
int __FUNC_peerconn_send_to_peer(void *bto,
                                        const void* pr,
                                        const void *data,
                                        const int len)
{
    const bt_peer_t * peer = pr;
    bt_client_t *bt = bto;

    return bt->net.peer_send(&bt->net_udata, peer->net_peerid, data, len);
}

int __FUNC_peerconn_pollblock(void *bto,
        void* bitfield, bt_block_t * blk)
{

    bitfield_t * peer_bitfield = bitfield;
    bt_client_t *bt = bto;
    bt_piece_t *pce;

    if ((pce = bt_piecedb_poll_best_from_bitfield(bt->db, peer_bitfield)))
    {
        assert(!bt_piece_is_complete(pce));
        assert(!bt_piece_is_fully_requested(pce));
        bt_piece_poll_block_request(pce, blk);
        return 0;
    }
    else
    {
        return -1;
    }
}

/*  return how many we've read */
int __FUNC_peerconn_recv_from_peer(void *bto,
        void* pr,
        char *buf,
        int *len)
{
    bt_client_t *bt = bto;
    bt_peer_t * peer = pr;

    return bt->net.peer_recv_len(&bt->net_udata, peer->net_peerid, buf, len);
}

void __FUNC_peerconn_send_have(void* caller, void* peer, void* udata)
{
    if (bt_peerconn_is_active(peer))
        bt_peerconn_send_have(peer, bt_piece_get_idx(udata));
}

/**
 * Received a block from a peer
 *
 * @param peer : peer received from
 * */
int __FUNC_peerconn_pushblock(void *bto,
                                    void* pr,
                                    bt_block_t * block,
                                    void *data)
{
    bt_peer_t * peer = pr;
    bt_client_t *bt = bto;
    bt_piece_t *pce;

    pce = bt_client_get_piece(bt, block->piece_idx);

    assert(pce);

    bt_piece_write_block(pce, NULL, block, data);
//    bt_filedumper_write_block(bt->fd, block, data);

    if (bt_piece_is_complete(pce))
    {
        /*  send have to all peers */
        if (bt_piece_is_valid(pce))
        {
            int ii;

            __log(bt, NULL, "client,piece downloaded,pieceidx=%d\n",
                  bt_piece_get_idx(pce));

            /* send HAVE messages to all peers */
            bt_peermanager_forall(bt->pm,bt,pce,__FUNC_peerconn_send_have);
        }

        bt_piecedb_print_pieces_downloaded(bt->db);

        /* dump everything to disk if the whole download is complete */
        if (bt_piecedb_all_pieces_are_complete(bt))
        {
            bt->am_seeding = 1;
//            bt_diskcache_disk_dump(bt->dc);
        }
    }

    return 1;
}

void __FUNC_peerconn_log(void *bto, void *src_peer, const char *buf, ...)
{
    bt_client_t *bt = bto;

    bt_peer_t *peer = src_peer;

    char buffer[256];

    sprintf(buffer, "pwp,%s,%s\n", peer->peer_id, buf);
    bt->func_log(bt->log_udata, NULL, buffer);
}

int __FUNC_peerconn_disconnect(void *bto,
        void* pr, char *reason)
{
    bt_peer_t * peer = pr;
    __log(bto,NULL,"disconnecting,%s\n", reason);
    return 1;
}

int __FUNC_peerconn_connect(void *bto, void *pc, void* pr)
{
    bt_peer_t * peer = pr;
    bt_client_t *bt = bto;

    /* the remote peer will have always send a handshake */
    if (!bt->net.peer_connect)
    {
        return 0;
    }

    if (0 == bt->net.peer_connect(&bt->net_udata, peer->ip,
                                  peer->port, &peer->net_peerid))
    {
        __log(bto,NULL,"failed connection to peer\n");
        return 0;
    }

    return 1;
}

int __FUNC_peerconn_pieceiscomplete(void *bto, void *piece)
{
    bt_client_t *bt = bto;
    bt_piece_t *pce = piece;

    return bt_piece_is_complete(pce);
}

void __FUNC_peerconn_write_block_to_stream(void* pce, bt_block_t * blk, byte ** msg)
{
    bt_piece_write_block_to_stream(pce, blk, msg);
}

//-----------------------------------------------------------------------------





typedef struct {

    void **peerconnects;
    int npeers;
    void* cfg;

} bt_peermanager_t;

int bt_peermanager_contains(void *pm, const char *ip, const int port)
{
    bt_peermanager_t *me = pm;
    int ii;

    for (ii = 0; ii < me->npeers; ii++)
    {
        bt_peer_t *peer;

        peer = bt_peerconn_get_peer(me->peerconnects[ii]);
        if (!strcmp(peer->ip, ip) && atoi(peer->port) == port)
        {
            return 1;
        }
    }
    return 0;
}

void *bt_peermanager_netpeerid_to_peerconn(void * pm, const int netpeerid)
{
    bt_peermanager_t *me = pm;
    int ii;

    for (ii = 0; ii < me->npeers; ii++)
    {
        void *pc;
        bt_peer_t *peer;

        pc = me->peerconnects[ii];

//        if (!bt_peerconn_is_active(pc))
//            continue;
        peer = bt_peerconn_get_peer(pc);
        if (peer->net_peerid == netpeerid)
            return pc;
    }

    assert(FALSE);
    return NULL;
}

/**
 * Add the peer.
 * Initiate connection with 
 *
 * @return freshly created bt_peer
 */
void *bt_peermanager_add_peer(void *pm,
                              const char *peer_id,
                              const int peer_id_len,
                              const char *ip, const int ip_len, const int port)
{
    bt_peermanager_t *me = pm;
    void *pc;
    bt_peer_t *peer;

    /* prevent dupes.. */
    if (bt_peermanager_contains(me, ip, port))
    {
        return NULL;
    }

    peer = calloc(1, sizeof(bt_peer_t));

    /*  'compact=0'
     *  doesn't use peerids.. */
    if (peer_id)
    {
        asprintf(&peer->peer_id, "00000000000000000000");
    }
    else
    {
        asprintf(&peer->peer_id, "", peer_id_len, peer_id);
    }
    asprintf(&peer->ip, "%.*s", ip_len, ip);
    asprintf(&peer->port, "%d", port);

    /* create a peer connection for this peer */
    pwp_connection_functions_t funcs = {
        .send = __FUNC_peerconn_send_to_peer,
        .recv = __FUNC_peerconn_recv_from_peer,
        .pushblock = __FUNC_peerconn_pushblock,
        .pollblock = __FUNC_peerconn_pollblock,
        .disconnect = __FUNC_peerconn_disconnect,
        .connect = __FUNC_peerconn_connect,
        .getpiece = bt_client_get_piece,
        .write_block_to_stream = __FUNC_peerconn_write_block_to_stream,
        .piece_is_complete = __FUNC_peerconn_pieceiscomplete,
    };

    peer->pc = pc = bt_peerconn_new();
    bt_peerconn_set_piece_info(pc,
            config_get_int(me->cfg,"npieces"),
            config_get_int(me->cfg,"piece_length"));
    bt_peerconn_set_peer(pc, peer);
//    bt_peerconn_set_my_peer_id(pc, );
//    bt_peerconn_set_their_peer_id(pc, );
    bt_peerconn_set_infohash(pc, config_get(me->cfg,"infohash"));

//    __log(bto,NULL,"adding peer: ip:%.*s port:%d\n", ip_len, ip, port);

    me->npeers++;
    me->peerconnects = realloc(me->peerconnects, sizeof(void *) * me->npeers);
    me->peerconnects[me->npeers - 1] = pc;
    return peer;
}

/**
 * Remove the peer.
 * Disconnect the peer
 *
 * @todo add disconnection functionality
 *
 * @return 1 on sucess; otherwise 0
 */
#if 0
int bt_peermanager_remove_peer(void *pm, const int peerid)
{
//    bt_peermanager_t *me = pm;
//    me->peerconnects[me->npeers - 1]

//    bt_leeching_choker_add_peer(me->lchoke, peer);
    return 1;
}
#endif

void bt_peermanager_forall(
        void* pm,
        void* caller,
        void* udata,
        void (*run)(void* caller, void* peer, void* udata))
{
    bt_peermanager_t *me = pm;
    int ii;

    for (ii = 0; ii < me->npeers; ii++)
    {
        void *pc;

        pc = me->peerconnects[ii];
        run(caller,pc,udata);
    }
}

int bt_peermanager_count(void* pm)
{
    bt_peermanager_t* me = pm;
    return me->npeers;
}

void bt_peermanager_set_config(void* pm, void* cfg)
{
    bt_peermanager_t* me = pm;

    me->cfg = cfg;
}

void* bt_peermanager_new()
{
    bt_peermanager_t* me;

    me = calloc(1,sizeof(bt_peermanager_t));
    return me;
}
