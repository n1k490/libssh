/*
 * crypt.c - blowfish-cbc code
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2003 by Aris Adamantiadis
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 * vim: ts=2 sw=2 et cindent
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef OPENSSL_CRYPTO
#include <openssl/blowfish.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

#include "libssh/priv.h"
#include "libssh/crypto.h"

u32 packet_decrypt_len(SSH_SESSION *session, char *crypted){
  u32 decrypted;

  if (session->current_crypto) {
    if (packet_decrypt(session, crypted,
          session->current_crypto->in_cipher->blocksize) < 0) {
      return 0;
    }
  }

  memcpy(&decrypted,crypted,sizeof(decrypted));
  ssh_log(session, SSH_LOG_PACKET,
      "Packet size decrypted: %lu (0x%lx)",
      (long unsigned int) ntohl(decrypted),
      (long unsigned int) ntohl(decrypted));
  return ntohl(decrypted);
}

int packet_decrypt(SSH_SESSION *session, void *data,u32 len) {
  struct crypto_struct *crypto = session->current_crypto->in_cipher;
  char *out = NULL;

  out = malloc(len);
  if (out == NULL) {
    return -1;
  }

  ssh_log(session,SSH_LOG_PACKET, "Decrypting %d bytes", len);

#ifdef HAVE_LIBGCRYPT
  crypto->set_decrypt_key(crypto,session->current_crypto->decryptkey,session->current_crypto->decryptIV);
  crypto->cbc_decrypt(crypto,data,out,len);
#elif defined HAVE_LIBCRYPTO
  crypto->set_decrypt_key(crypto,session->current_crypto->decryptkey);
  crypto->cbc_decrypt(crypto,data,out,len,session->current_crypto->decryptIV);
#endif

  memcpy(data,out,len);
  memset(out,0,len);

  SAFE_FREE(out);
  return 0;
}

unsigned char * packet_encrypt(SSH_SESSION *session,void *data,u32 len){
    struct crypto_struct *crypto;
    HMACCTX ctx;
    char *out;
    unsigned int finallen;
    u32 seq=ntohl(session->send_seq);
    if(!session->current_crypto)
        return NULL; /* nothing to do here */
    crypto= session->current_crypto->out_cipher;
    ssh_log(session,SSH_LOG_PACKET,"encrypting packet with seq num: %d, len: %d",session->send_seq,len);
#ifdef HAVE_LIBGCRYPT
    crypto->set_encrypt_key(crypto,session->current_crypto->encryptkey,session->current_crypto->encryptIV);
#elif defined HAVE_LIBCRYPTO
    crypto->set_encrypt_key(crypto,session->current_crypto->encryptkey);
#endif
    out = malloc(len);
    if (out == NULL) {
      return NULL;
    }
    if(session->version==2){
        ctx = hmac_init(session->current_crypto->encryptMAC,20,HMAC_SHA1);
        if (ctx == NULL) {
          SAFE_FREE(out);
          return NULL;
        }
        hmac_update(ctx,(unsigned char *)&seq,sizeof(u32));
        hmac_update(ctx,data,len);
        hmac_final(ctx,session->current_crypto->hmacbuf,&finallen);
#ifdef DEBUG_CRYPTO
        ssh_print_hexa("mac :",data,len);
        if(finallen!=20)
            printf("Final len is %d\n",finallen);
        ssh_print_hexa("packet hmac",session->current_crypto->hmacbuf,20);
#endif
    }
#ifdef HAVE_LIBGCRYPT
    crypto->cbc_encrypt(crypto,data,out,len);
#elif defined HAVE_LIBCRYPTO
    crypto->cbc_encrypt(crypto,data,out,len,session->current_crypto->encryptIV);
#endif
    memcpy(data,out,len);
    memset(out,0,len);
    free(out);
    if(session->version==2)
        return session->current_crypto->hmacbuf;
    else
        return NULL;
}

/* TODO FIXME think about the return value isn't 0 enough and -1 on error */
int packet_hmac_verify(SSH_SESSION *session,BUFFER *buffer,unsigned char *mac){
    HMACCTX ctx;
    unsigned char hmacbuf[EVP_MAX_MD_SIZE];
    unsigned int len;
    u32 seq=htonl(session->recv_seq);
    ctx=hmac_init(session->current_crypto->decryptMAC,20,HMAC_SHA1);
    if (ctx == NULL) {
      return -1;
    }
    hmac_update(ctx,(unsigned char *)&seq,sizeof(u32));
    hmac_update(ctx,buffer_get(buffer),buffer_get_len(buffer));
    hmac_final(ctx,hmacbuf,&len);
#ifdef DEBUG_CRYPTO
    ssh_print_hexa("received mac",mac,len);
    ssh_print_hexa("Computed mac",hmacbuf,len);
    ssh_print_hexa("seq",(unsigned char *)&seq,sizeof(u32));
#endif
    return memcmp(mac,hmacbuf,len);
}
