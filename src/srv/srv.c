/**
 * @file server.c
 * @brief
 * @author Shengyu Zhang <silverrainz@outlook.com>
 * @version 1.0
 * @date 2016-07-19
 */

#define __LOG_ON
// #define __DBG_ON

#include <string.h>
#include <gtk/gtk.h>

#include "srv.h"
#include "srv_event.h"

#include "sirc_cmd.h"

#include "meta.h"
#include "srain.h"
#include "log.h"

Server* server_new(const char *name,
        const char *host,
        int port,
        const char *passwd,
        bool ssl,
        const char *enconding,
        const char *nick,
        const char *username,
        const char *realname){
    if (!host) return NULL;
    if (!nick) return NULL;
    if (!name) name = host;
    if (!passwd) enconding = "";
    if (!enconding) enconding = "UTF-8";
    if (!username) username = PACKAGE_NAME;
    if (!realname) realname = nick;

    Server *srv = g_malloc0(sizeof(Server));

    srv->port = port;
    srv->ssl = ssl;
    srv->stat = SERVER_UNCONNECTED;
    /* srv->chat_list = NULL; */ // by g_malloc0()

    g_strlcpy(srv->name, name, sizeof(srv->name));
    g_strlcpy(srv->host, host, sizeof(srv->host));
    g_strlcpy(srv->passwd, passwd, sizeof(srv->passwd));

    /* srv->user */
    srv->user.me = TRUE;
    g_strlcpy(srv->user.nick, nick, sizeof(srv->user.nick));
    g_strlcpy(srv->user.username, username, sizeof(srv->user.username));
    g_strlcpy(srv->user.realname, realname, sizeof(srv->user.realname));

    /* Get UI & IRC handler */
    srv->ui = sui_new_session(META_SERVER, srv->name, CHAT_SERVER, srv);
    srv->irc = sirc_new_session(srv);

    if (!srv->ui || !srv->irc){
        goto bad;
    }

    /* IRC event callbacks */
    srv->irc->events.connect = srv_event_connect;
    srv->irc->events.disconnect = srv_event_disconnect;
    srv->irc->events.ping = srv_event_ping;
    srv->irc->events.welcome = srv_event_welcome;
    srv->irc->events.nick = srv_event_nick;
    srv->irc->events.quit = srv_event_quit;
    srv->irc->events.join = srv_event_join;
    srv->irc->events.part = srv_event_part;
    srv->irc->events.mode = srv_event_mode;
    srv->irc->events.umode = srv_event_umode;
    srv->irc->events.topic = srv_event_topic;
    srv->irc->events.kick = srv_event_kick;
    srv->irc->events.channel = srv_event_channel;
    srv->irc->events.privmsg = srv_event_privmsg;
    srv->irc->events.notice = srv_event_notice;
    srv->irc->events.channel_notice = srv_event_channel_notice;
    srv->irc->events.invite = srv_event_invite;
    srv->irc->events.ctcp_action = srv_event_ctcp_action;
    srv->irc->events.numeric = srv_event_numeric;

    return srv;

bad:
    // server_free();
    return NULL;
}

void server_free(Server *srv){
    if (srv->chat_list != NULL){
        while (srv->chat_list) {
            Chat *chat = srv->chat_list->data;
            server_rm_chat(srv, chat->name);
        }
    }

    if (srv->irc != NULL){
        sirc_free_session(srv->irc);
    }

    if (srv->ui != NULL){
        sui_free_session(srv->ui);
    }

    g_free(srv);
}

int server_connect(Server *srv){
    srv->stat = SERVER_CONNECTING;
    sirc_connect(srv->irc, srv->host, srv->port);

    return SRN_OK;
}

void server_disconnect(Server *srv){
    srv->stat = SERVER_DISCONNECTED;
    sirc_disconnect(srv->irc);
}

int server_add_chat(Server *srv, const char *name, const char *passwd){
    GList *lst;
    Chat *chat;

    if (!passwd) passwd = "";

    lst = srv->chat_list;
    while (lst) {
        chat = lst->data;
        if (strcasecmp(chat->name, name) == 0){
            return SRN_ERR;
        }
        lst = g_list_next(lst);
    }

    chat = g_malloc0(sizeof(Chat));

    chat->srv = srv;
    chat->me = NULL;
    chat->user_list = NULL;
    chat->ui = sui_new_session(name, srv->name,
            SIRC_IS_CHAN(name) ? CHAT_CHANNEL : CHAT_PRIVATE, srv); // ??

    g_strlcpy(chat->name, name, sizeof(chat->name));
    g_strlcpy(chat->passwd, passwd, sizeof(chat->passwd));

    srv->chat_list = g_list_append(srv->chat_list, chat);

    return SRN_OK;
}

int server_rm_chat(Server *srv, const char *name){
    GList *lst;
    Chat *chat;

    lst = srv->chat_list;
    while (lst) {
        chat = lst->data;
        if (strcasecmp(chat->name, name) == 0){
            sui_free_session(chat->ui);
            // rm user_list
            g_free(chat);
            srv->chat_list = g_list_delete_link(srv->chat_list, lst);
            return SRN_OK;
        }
    }

    return SRN_ERR;
}

Chat* server_get_chat(Server *srv, const char *name) {
    GList *lst;
    Chat *chat;

    lst = srv->chat_list;
    while (lst) {
        chat = lst->data;
        if (strcasecmp(chat->name, name) == 0){
            return chat;
        }
        lst = g_list_next(lst);
    }

    return NULL;
}

int chat_add_user(Chat *chat, const char *nick, UserType type){
    GList *lst;
    User *user;

    lst = chat->user_list;
    while (lst){
        user = lst->data;
        if (strcasecmp(user->nick, nick) == 0){
            return SRN_ERR;
        }
        lst = g_list_next(lst);
    }

    user = g_malloc0(sizeof(User));

    user->me = FALSE;
    user->type = type;

    g_strlcpy(user->nick, nick, sizeof(user->nick));
    // g_strlcpy(user->username, username, sizeof(user->username));
    // g_strlcpy(user->realnaem, realname, sizeof(user->realname));
    chat->user_list = g_list_append(chat->user_list, user);

    sui_add_user(chat->ui, nick, type);

    return SRN_OK;
}

int chat_rm_user(Chat *chat, const char *nick){
    GList *lst;
    User *user;

    lst = chat->user_list;
    while (lst){
        user = lst->data;
        if (strcasecmp(user->nick, nick) == 0){
            chat->user_list = g_list_delete_link(chat->user_list, lst);
            sui_rm_user(chat->ui, user->nick);
            g_free(user);

            return SRN_OK;
        }
        lst = g_list_next(lst);
    }

    return SRN_ERR;
}

User* chat_get_user(Chat *chat, const char *nick){
    User *user;
    GList *lst;

    lst = chat->user_list;
    while (lst){
        user = lst->data;
        if (strcasecmp(user->nick, nick) == 0){
            return user;
        }
        lst = g_list_next(lst);
    }

    return NULL;
}

void srv_init(){ }

void srv_finalize(){ }
