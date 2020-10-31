/*
 *  Copyright (C) 2006-2018, ARM Limited, All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 */

/**
 * \file writer.h
 *
 * \brief This file defines writer objects, which together with their
 *        sibling reader objects form the basis for the communication
 *        between the various layers of the Mbed TLS messaging stack,
 *        as well as the communication between the messaging stack and
 *        the (D)TLS handshake protocol implementation.
 *
 * Writers provide a means of communication between
 * - a 'provider' supplying buffers to hold outgoing data, and
 * - a 'consumer' writing data into these buffers.
 * Both the size of the data buffers the provider prepares and the size of
 * chunks in which the consumer writes the data are variable and may be
 * different. It is the writer's responsibility to do the necessary copying
 * and pointer arithmetic.
 *
 * For example, the provider might be the [D]TLS record layer, offering
 * to protect and transport data in records of varying size (depending
 * on the current configuration and the amount of data left in the current
 * datagram, for example), while the consumer would be the handshake logic
 * layer which needs to write handshake messages. The size of handshake
 * messages are entirely independent of the size of records used to transport
 * them, and the writer helps to both split large handshake messages across
 * multiple records, and to pack multiple small handshake messages into
 * a single record. This example will be elaborated upon in the next paragraph.
 *
 * Basic flow of operation:
 * First, the provider feeds an outgoing data buffer to the writer, transferring
 * it from 'providing' to 'consuming' state; in the example, that would be record
 * layer providing the plaintext buffer for the next outgoing record. The
 * consumer subsequently fetches parts of the buffer and writes data to them,
 * which might happen multiple times; in the example, the handshake logic
 * layer might request and fill a buffer for each handshake message in the
 * current outgoing flight, and these requests would be served from successive
 * chunks in the same record plaintext buffer if size permits. Once the consumer
 * is done, the provider revokes the writer's access to the data buffer,
 * putting the writer back to providing state, and processes the data provided
 * in the outgoing data buffer; in the example, that would be the record layer
 * encrypting the record and dispatching it to the underlying transport.
 * Afterwards, the provider feeds another outgoing data buffer to the writer
 * and the cycle starts again.
 * In the event that a consumer's request cannot be fulfilled on the basis of
 * the outgoing data buffer provided by the provider (in the example,
 * the handshake layer might attempt to send a 4KB CRT chain but the current
 * record size offers only 2KB), the writer transparently offers a temporary
 * 'queue' buffer to hold the data to the consumer. The contents of this queue
 * buffer will be gradually split among the next outgoing data buffers when
 * the provider subsequently provides them; in the example, the CRT chain would
 * be split amont multiple records when the record layer hands more plaintext
 * buffers to the writer. The details of this process are left to the writer
 * and are opaque both to the consumer and the provider.
 *
 * Abstract models:
 * From the perspective of the consumer, the state of the writer is a
 * potentially empty list of output buffers that the writer has provided
 * to the consumer. New buffers can be requested through calls to
 * mbedtls_writer_get(), while previously obtained output buffers can be
 * marked processed through calls to mbedtls_writer_commit(), emptying the
 * list of output buffers and invalidating them from the consumer's perspective.
 *
 */

#ifndef MBEDTLS_WRITER_H
#define MBEDTLS_WRITER_H

#include <stdio.h>
#include <stdint.h>

#include "common.h"

struct mbedtls_writer;
typedef struct mbedtls_writer mbedtls_writer;

struct mbedtls_writer_ext;
typedef struct mbedtls_writer_ext mbedtls_writer_ext;

/*
 * Error codes returned from the writer.
 */

/** An attempt was made to reclaim a buffer from the writer,
 *  but the buffer hasn't been fully used up, yet.            */
#define MBEDTLS_ERR_WRITER_DATA_LEFT             MBEDTLS_WRITER_MAKE_ERROR( 0x1 )
/** The validation of input parameters failed.                */
#define MBEDTLS_ERR_WRITER_INVALID_ARG           MBEDTLS_WRITER_MAKE_ERROR( 0x2 )
/** The provided outgoing data buffer was not large enough to
 *  hold all queued data that's currently pending to be
 *  delivered.                                                */
#define MBEDTLS_ERR_WRITER_NEED_MORE             MBEDTLS_WRITER_MAKE_ERROR( 0x3 )
/** The requested operation is not possible
 *  in the current state of the writer.                       */
#define MBEDTLS_ERR_WRITER_OPERATION_UNEXPECTED  MBEDTLS_ERR_MPS_OPERATION_UNEXPECTED
/** The remaining amount of space for outgoing data is not
 *  sufficient to serve the user's request. The current
 *  outgoing data buffer must be reclaimed, dispatched,
 *  and a fresh outgoing data buffer must be fed to the
 *  writer.                                                   */
#define MBEDTLS_ERR_WRITER_OUT_OF_DATA           MBEDTLS_WRITER_MAKE_ERROR( 0x5 )
/** A write-request was issued to the extended writer that
 *  exceeds the bounds of the most recently added group.      */
#define MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION      MBEDTLS_WRITER_MAKE_ERROR( 0x9 )
/** The extended writer has reached the maximum number of
 *  groups, and another group cannot be added.                */
#define MBEDTLS_ERR_WRITER_TOO_MANY_GROUPS       MBEDTLS_WRITER_MAKE_ERROR( 0xa )

/** The identifier to use in mbedtls_writer_reclaim() to
 *  force the reclamation of the outgoing data buffer even
 *  if there's space remaining.                               */
#define MBEDTLS_WRITER_RECLAIM_FORCE 1
/** The identifier to use in mbedtls_writer_reclaim() if
 *  the call should only succeed if the current outgoing data
 *  buffer has been fully used up.                            */
#define MBEDTLS_WRITER_RECLAIM_NO_FORCE 0

/** \brief The type of states for the reader.
 *
 *  Possible values are:
 *  - #MBEDTLS_WRITER_PROVIDING (initial state)
 *    The writer awaits buffers for holding outgoing
 *    data to be assigned to it via mbedtls_writer_feed().
 *  - #MBEDTLS_WRITER_PRODUCING
 *    The writer has buffers to serve write requests from.
 **/
typedef unsigned char mbedtls_writer_state_t;
#define MBEDTLS_WRITER_PROVIDING ( (mbedtls_writer_state_t) 0)
#define MBEDTLS_WRITER_CONSUMING ( (mbedtls_writer_state_t) 1)

struct mbedtls_writer
{
    /** The current buffer to hold outgoing data.      */
    unsigned char *out;
    /** The queue buffer from which to serve write requests that would
     *  exceed the current outgoing data buffer's bounds. May be \c NULL. */
    unsigned char *queue;
    /** The size in bytes of the outgoing data buffer \c out. */
    mbedtls_mps_stored_size_t out_len;
    /** The length of the queue buffer \c queue. */
    mbedtls_mps_stored_size_t queue_len;

    /** The offset from the beginning of the outgoing data buffer indicating
     *  the amount of data that the user has already finished writing.
     *
     *  Note: When a queue buffer is in use, this may be larger than the length
     *        of the outgoing data buffer, and is computed as if the outgoing
     *        data buffer was immediately followed by the queue buffer.
     *
     * This is only used when the writer is in consuming state, i.e.
     * <code>state == MBEDTLS_WRITER_CONSUMING</code>; in this case, its value
     * is smaller or equal to <code>out_len + queue_len</code>.
     */
    mbedtls_mps_stored_size_t committed;

    /** The offset from the beginning of the outgoing data buffer of the
     *  end of the last fragment handed to the user.
     *
     *  Note: When a queue buffer is in use, this may be larger than the
     *  length of the outgoing data buffer, and is computed as if the outgoing
     *  data buffer was immediately followed by the queue buffer.
     *
     *  This is only used when the writer is in consuming state,
     *  i.e. <code>state == MBEDTLS_WRITER_CONSUMING</code>; in this case,
     *  its value is smaller or equal to <code>out_len + queue_len</code>.
     */
    mbedtls_mps_stored_size_t end;

    /** In consuming state, this denotes the size of the overlap between the
     *  queue and the current out buffer, once <code>end > out_len</code>.
     *  If <code>end < out_len</code>, its value is \c 0.
     *  In providing state, this denotes the amount of data from the queue that
     *  has already been copied to some outgoing data buffer.
     */
    mbedtls_mps_stored_size_t queue_next;
    /** The amount of data within the queue buffer that hasn't been copied to
     *  some outgoing data buffer yet. This is only used in providing state, and
     *  if the writer uses a queue (<code>queue != NULL</code>), and in this
     *  case its value is at most <code>queue_len - queue_next</code>.
     */
    mbedtls_mps_stored_size_t queue_remaining;
    /** The writer's state. See ::mbedtls_writer_state_t. */
    mbedtls_writer_state_t state;
};

/** Configures whether commits to the extended writer should be passed
 *  through to the underlying writer or not. Possible values are:
 *  - #MBEDTLS_WRITER_EXT_PASS
 *  - #MBEDTLS_WRITER_EXT_HOLD
 *  - #MBEDTLS_WRITER_EXT_BLOCK.
 */
typedef unsigned char mbedtls_writer_ext_passthrough_t;
#define MBEDTLS_WRITER_EXT_PASS   ( (mbedtls_writer_ext_passthrough_t) 0 )
#define MBEDTLS_WRITER_EXT_HOLD   ( (mbedtls_writer_ext_passthrough_t) 1 )
#define MBEDTLS_WRITER_EXT_BLOCK  ( (mbedtls_writer_ext_passthrough_t) 2 )

/** The type of indices for groups in extended writers. */
typedef unsigned char mbedtls_writer_ext_grp_index_t;

/* INTERNAL NOTE:
 *
 * The value for MBEDTLS_WRITER_MAX_GROUPS needs to be revisited once
 * writers are comprehensively used in the message writing functions
 * used by the handshake logic layer. Reducing this value saves a few
 * bytes in ::mbedtls_writer_ext.
 */
/** The maximum number of nested groups that can be opened in an
 *  extended writer. */
#define MBEDTLS_WRITER_MAX_GROUPS ( (mbedtls_writer_ext_grp_index_t) 5 )

struct mbedtls_writer_ext
{
    /** The underlying writer object - may be \c NULL. */
    mbedtls_writer *wr;
    /** The offsets marking the ends of the currently active groups.
     *  The first <code>cur_grp + 1</code> entries are valid and always
     *  weakly descending (subsequent groups are subgroups of their
     *  predecessors ones).  */
    mbedtls_mps_stored_size_t grp_end[MBEDTLS_WRITER_MAX_GROUPS];
    /** The offset of the first byte of the next chunk.  */
    mbedtls_mps_stored_size_t ofs_fetch;
    /**< The offset of first byte beyond the last committed chunk. */
    mbedtls_mps_stored_size_t ofs_commit;
    /** The 0-based index of the currently active group.
     *  The group of index 0 always exists and represents
     *  the entire logical message buffer. */
    mbedtls_writer_ext_grp_index_t cur_grp;
    /** Indicates whether commits should be passed to the underlying writer.
     *  See ::mbedtls_writer_ext_passthrough_t. */
    mbedtls_writer_ext_passthrough_t passthrough;
};

/**
 * \brief           Initialize a writer object
 *
 * \param writer    The writer to be initialized.
 * \param queue     The buffer to be used as dispatch queue if
 *                  buffer provided via mbedtls_writer_feed()
 *                  isn't sufficient.
 * \param queue_len The size in Bytes of \p queue.
 */
void mbedtls_writer_init( mbedtls_writer *writer,
                          unsigned char *queue,
                          mbedtls_mps_size_t queue_len );

/**
 * \brief           Free a writer object
 *
 * \param writer    The writer to be freed.
 */
void mbedtls_writer_free( mbedtls_writer *writer );

/**
 * \brief           Pass output buffer to the writer.
 *
 *                  This function is used to transition the writer
 *                  from providing to consuming state.
 *
 * \param writer    The writer context to be used.
 * \param buf       The buffer that outgoing data can be written to
 *                  and that the writer should manage.
 * \param buflen    The length of the outgoing data buffer.
 *
 * \return          \c 0 on success. In this case, the writer is
 *                  in consuming state afterwards.
 * \return          #MBEDTLS_ERR_WRITER_OPERATION_UNEXPECTED if
 *                  the writer is not in providing state. In this case,
 *                  the writer is unmodified and can still be used.
 *                  In particular, the writer stays in consuming state.
 * \return          #MBEDTLS_ERR_WRITER_NEED_MORE if the provided
 *                  outgoing data buffer was completely filled by data
 *                  that had been internally queued in the writer.
 *                  In this case, the writer stays in consuming state,
 *                  but the content of the output buffer is ready to be
 *                  dispatched in the same way as after a cycle of calls
 *                  to mbedtls_writer_feed(), mbedtls_writer_get(),
 *                  mbedtls_writer_commit(), mbedtls_writer_reclaim().
 * \return          Another negative error code otherwise. In this case,
 *                  the state of the writer is unspecified and it must
 *                  not be used anymore.
 *
 */
int mbedtls_writer_feed( mbedtls_writer *writer,
                         unsigned char *buf,
                         mbedtls_mps_size_t buflen );

/**
 * \brief           Attempt to reclaim output buffer from writer,
 *
 *                  This function is used to transition the writer
 *                  from consuming to providing state.
 *
 * \param writer    The writer context to be used.
 * \param queued    The address at which to store the amount of
 *                  outgoing data that has been queued. May be \c NULL
 *                  if this information is not required.
 * \param force     Indicates whether the output buffer should
 *                  be reclaimed even if there's space left.
 *                  Must be either #MBEDTLS_WRITER_RECLAIM_FORCE
 *                  or #MBEDTLS_WRITER_RECLAIM_NO_FORCE.
 *
 * \return          \c 0 on success. In this case, the writer is in
 *                  providing state afterwards.
 * \return          #MBEDTLS_ERR_WRITER_OPERATION_UNEXPECTED if
 *                  the writer is not in consuming state. In this case,
 *                  the writer is unmodified and can still be used.
 *                  In particular, the writer stays in providing state.
 * \return          #MBEDTLS_ERR_WRITER_DATA_LEFT if there is space
 *                  left to be written in the output buffer.
 *                  In this case, the writer stays in consuming state.
 * \return          Another negative error code otherwise. In this case,
 *                  the state of the writer is unspecified and it must
 *                  not be used anymore.
 *
 *                  On success, \c *queued contains the number of bytes that
 *                  have been queued internally in the writer and will be
 *                  written to the next buffer(s) that is fed to the writer.
 *
 */
int mbedtls_writer_reclaim( mbedtls_writer *writer,
                            mbedtls_mps_size_t *queued,
                            mbedtls_mps_size_t *written,
                            int force );

/**
 * \brief           Check how many bytes have already been written
 *                  to the current output buffer.
 *
 * \param writer    Writer context
 * \param written   Pointer to receive amount of data already written.
 *
 * \return          \c 0 on success.
 * \return          A negative error code \c MBEDTLS_ERR_WRITER_XXX on failure.
 *
 */
int mbedtls_writer_bytes_written( mbedtls_writer *writer,
                                  mbedtls_mps_size_t *written );

/**
 * \brief           Signal that all output buffers previously obtained
 *                  from mbedtls_writer_get() are ready to be dispatched.
 *
 *                  This function must only be called when the writer
 *                  is in consuming state.
 *
 * \param writer    The writer context to use.
 *
 * \note            After this function has been called, all
 *                  output buffers obtained from prior calls to
 *                  mbedtls_writer_get() are invalid and must not
 *                  be used anymore.
 *
 * \return          \c 0 on success. In this case, the writer
 *                  stays in consuming state.
 * \return          #MBEDTLS_ERR_WRITER_OPERATION_UNEXPECTED
 *                  if the writer is not in consuming state.
 *                  In this case, the writer is unchanged and
 *                  can still be used.
 * \return          Another negative error code otherwise. In this case,
 *                  the state of the writer is unspecified and it must
 *                  not be used anymore.
 *
 */
int mbedtls_writer_commit( mbedtls_writer *writer );

/**
 * \brief           Signal that parts of the output buffers obtained
 *                  from mbedtls_writer_get() are ready to be dispatched.
 *
 *                  This function must only be called when the writer
 *                  is in consuming state.
 *
 * \param writer    The writer context to use.
 * \param omit      The number of bytes at the end of the last output
 *                  buffer obtained from mbedtls_writer_get() that should
 *                  not be committed.
 *
 * \note            After this function has been called, all
 *                  output buffers obtained from prior calls to
 *                  mbedtls_writer_get() are invalid and must not
 *                  be used anymore.
 *
 * \return          \c 0 on success. In this case, the writer
 *                  stays in consuming state.
 * \return          #MBEDTLS_ERR_WRITER_OPERATION_UNEXPECTED
 *                  if the writer is not in consuming state.
 *                  In this case, the writer is unchanged and
 *                  can still be used.
 * \return          Another negative error code otherwise. In this case,
 *                  the state of the writer is unspecified and it must
 *                  not be used anymore.
 *
 */
int mbedtls_writer_commit_partial( mbedtls_writer *writer,
                                   mbedtls_mps_size_t omit );

/**
 * \brief           Request buffer to hold outbound data.
 *
 *                  This function must only be called when the writer
 *                  is in consuming state.
 *
 * \param writer    The writer context to use.
 * \param desired   The desired size of the outgoing data buffer.
 * \param buffer    The address at which to store the address
 *                  of the outgoing data buffer on success.
 * \param buflen    The address at which to store the actual
 *                  size of the outgoing data buffer on success.
 *                  May be \c NULL (see below).
 *
 * \note            If \p buflen is \c NULL, the function fails
 *                  if it cannot provide an outgoing data buffer
 *                  of the requested size \p desired.
 *
 * \return          \c 0 on success. In this case, the writer
 *                  stays in consuming state.
 * \return          #MBEDTLS_ERR_WRITER_OPERATION_UNEXPECTED
 *                  if the writer is not in consuming state.
 *                  In this case, the writer is unchanged and
 *                  can still be used.
 * \return          #MBEDTLS_ERR_WRITER_OUT_OF_SPACE if there is not
 *                  enough space available to serve the request.
 *                  In this case, the writer remains intact, and
 *                  additional space can be provided by reclaiming
 *                  the current output buffer via mbedtls_writer_reclaim()
 *                  and feeding a new one via mbedtls_writer_feed().
 * \return          Another negative error code otherwise. In this case,
 *                  the state of the writer is unspecified and it must
 *                  not be used anymore.
 *
 */
int mbedtls_writer_get( mbedtls_writer *writer,
                        mbedtls_mps_size_t desired,
                        unsigned char **buffer,
                        mbedtls_mps_size_t *buflen );

/**
 * \brief           Initialize an extended writer object
 *
 * \param writer    The extended writer context to initialize.
 * \param size      The total size of the logical buffer to
 *                  be managed by the extended writer.
 *
 */
void mbedtls_writer_init_ext( mbedtls_writer_ext *writer,
                              mbedtls_mps_size_t size );

/**
 * \brief           Free an extended writer object
 *
 * \param writer    The extended writer context to be freed.
 */
void mbedtls_writer_free_ext( mbedtls_writer_ext *writer );

/**
 * \brief           Request buffer to hold outbound data.
 *
 * \param writer    The extended writer context to use.
 * \param desired   The desired size of the outgoing data buffer.
 * \param buffer    The address at which to store the address
 *                  of the outgoing data buffer on success.
 * \param buflen    The address at which to store the actual
 *                  size of the outgoing data buffer on success.
 *                  May be \c NULL (see below).
 *
 * \note            If \p buflen is \c NULL, the function fails
 *                  if it cannot provide an outgoing data buffer
 *                  of the requested size \p desired.
 *
 * \return          \c 0 on success. In this case, \c *buf holds the
 *                  address of a buffer of size \c *buflen
 *                  (if \c buflen != NULL) or \p desired
 *                  (if \c buflen is \c NULL).
 * \return          #MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION if the write
 *                  request exceeds the bounds of the current group.
 *
 */
int mbedtls_writer_get_ext( mbedtls_writer_ext *writer,
                            mbedtls_mps_size_t desired,
                            unsigned char **buffer,
                            mbedtls_mps_size_t *buflen );

/**
 * \brief           Signal that all output buffers previously obtained
 *                  from mbedtls_writer_get_ext() are ready to be dispatched.
 *
 * \param writer    The extended writer context to use.
 *
 * \note            After this function has been called, all
 *                  output buffers obtained from prior calls to
 *                  mbedtls_writer_get_ext() are invalid and must not
 *                  be accessed anymore.
 *
 * \return          \c 0 on success.
 * \return          A negative error code \c MBEDTLS_ERR_WRITER_XXX on failure.
 *
 */
int mbedtls_writer_commit_ext( mbedtls_writer_ext *writer );

/**
 * \brief           Signal that parts of the output buffers obtained
 *                  from mbedtls_writer_get_ext() are ready to be dispatched.
 *
 *                  This function must only be called when the writer
 *                  is in consuming state.
 *
 * \param writer    The writer context to use.
 * \param omit      The number of bytes at the end of the last output
 *                  buffer obtained from mbedtls_writer_get_ext() that should
 *                  not be committed.
 *
 * \note            After this function has been called, all
 *                  output buffers obtained from prior calls to
 *                  mbedtls_writer_get_ext() are invalid and must not
 *                  be used anymore.
 *
 * \return          \c 0 on success. In this case, the writer
 *                  stays in consuming state.
 * \return          #MBEDTLS_ERR_WRITER_OPERATION_UNEXPECTED
 *                  if the writer is not in consuming state.
 *                  In this case, the writer is unchanged and
 *                  can still be used.
 * \return          Another negative error code otherwise. In this case,
 *                  the state of the writer is unspecified and it must
 *                  not be used anymore.
 *
 */
int mbedtls_writer_commit_partial_ext( mbedtls_writer_ext *writer,
                                       mbedtls_mps_size_t omit );

/**
 * \brief            Open a new logical subbuffer.
 *
 * \param writer     The extended writer context to use.
 * \param group_size The offset of the end of the subbuffer
 *                   from the end of the last successful fetch.
 *
 * \return           \c 0 on success.
 * \return           #MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION if
 *                   the new group is not contained in the
 *                   current group. In this case, the extended
 *                   writer is unchanged and hence remains intact.
 * \return           #MBEDTLS_ERR_WRITER_TOO_MANY_GROUPS if the internal
 *                   threshold for the maximum number of groups exceeded.
 *                   This is an internal error, and it should be
 *                   statically verifiable that it doesn't occur.
 * \return           Another negative error code otherwise.
 *
 */
int mbedtls_writer_group_open( mbedtls_writer_ext *writer,
                               mbedtls_mps_size_t group_size );

/**
 * \brief            Close the most recently opened logical subbuffer.
 *
 * \param writer     The extended writer context to use.
 *
 * \return           \c 0 on success.
 * \return           #MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION if
 *                   the current logical subbuffer hasn't been
 *                   fully fetched and committed.
 * \return           #MBEDTLS_ERR_WRITER_NO_GROUP if there is no
 *                   group opened currently.
 * \return           Another negative error code otherwise.
 *
 */
int mbedtls_writer_group_close( mbedtls_writer_ext *writer );

/**
 * \brief           Attach a writer to an extended writer.
 *
 *                  Once a writer has been attached to an extended writer,
 *                  subsequent calls to mbedtls_writer_commit_ext() and
 *                  mbedtls_writer_get_ext() will be routed through the
 *                  corresponding calls to mbedtls_writer_commit() resp.
 *                  mbedtls_writer_get() after the extended writer has
 *                  done its bounds checks.
 *
 * \param wr_ext    The extended writer context to use.
 * \param wr        The writer to bind to the extended writer \p wr_ext.
 * \param pass      Indicates whether commits should be passed through
 *                  to the underlying writer. Possible values are:
 *                  - #MBEDTLS_WRITER_EXT_PASS: All commits are passed
 *                    through to the underlying reader. An unlimited
 *                    number of partial commits is possible.
 *                  - #MBEDTLS_WRITER_EXT_HOLD: Commits are remembered
 *                    but not yet passed to the underlying reader, and
 *                    only a single partial commit is possible, after
 *                    which the writer gets blocked. The information
 *                    about committed and uncommitted data is returned
 *                    when detaching the underlying writer via
 *                    mbedtls_writer_detach().
 *
 * \return          \c 0 on success.
 * \return          A negative error code \c MBEDTLS_ERR_WRITER_XXX on failure.
 *
 */
int mbedtls_writer_attach( mbedtls_writer_ext *wr_ext,
                           mbedtls_writer *wr,
                           int pass );
/**
 * \brief             Detach a writer from an extended writer.
 *
 * \param wr_ext      The extended writer context to use.
 * \param committed   Address to which to write the number of committed bytes
 *                    May be \c NULL if this information is not needed.
 * \param uncommitted Address to which to write the number of uncommitted bytes
 *                    May be \c NULL if this information is not needed.
 *
 * \return           \c 0 on success.
 * \return           A negative error code \c MBEDTLS_ERR_WRITER_XXX on failure.
 *
 */
int mbedtls_writer_detach( mbedtls_writer_ext *wr_ext,
                           mbedtls_mps_size_t *committed,
                           mbedtls_mps_size_t *uncommitted );

/**
 * \brief            Check if the extended writer has finished processing
 *                   the logical buffer it was setup with.
 *
 * \param writer     The extended writer context to use.
 *
 * \return           \c 0 if all groups opened via mbedtls_writer_group_open()
 *                   have been closed via mbedtls_writer_group_close(),
 *                   and the entire logical buffer as defined by the \c size
 *                   argument in mbedtls_writer_init_ext() has been processed.
 * \return           A negative \c MBEDTLS_ERR_WRITER_XXX error code otherwise.
 *
 */
int mbedtls_writer_check_done( mbedtls_writer_ext *writer );

/* /\** */
/*  * \brief           Get the writer's state */
/*  * */
/*  * \param writer    Writer context */
/*  * */
/*  * \return          The last state set at a call to mbedtls_writer_commit, */
/*  *                  or 0 if the reader is used for the first time and hasn't */
/*  *                  been paused before. */
/*  *\/ */
/* This has been included in the original MPS API specification,
 * but it hasn't been decided yet if we want to keep the state of
 * the writing within the writing or leave it to the user to save it
 * in an appropriate place, e.g. the handshake structure.
 * TODO: Make a decision, and potentially remove this declaration
 *       if the state is saved elsewhere.
 *       If this function is needed, the mbedtls_writer_commit
 *       function should get an additional state argument. */
/* int mbedtls_writer_state( mbedtls_writer_ext *writer ); */

#endif /* MBEDTLS_WRITER_H */
