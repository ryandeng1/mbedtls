/*
 *  Message Processing Stack, Writer implementation
 *
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
 *  This file is part of Mbed TLS (https://tls.mbed.org)
 */

#include "mbedtls/mps/writer.h"
#include "mbedtls/mps/trace.h"

#if defined(MBEDTLS_MPS_SEPARATE_LAYERS) ||     \
    defined(MBEDTLS_MPS_TOP_TRANSLATION_UNIT)

#if defined(MBEDTLS_MPS_TRACE)
static int trace_id = TRACE_BIT_WRITER;
#endif /* MBEDTLS_MPS_TRACE */

#include <string.h>

void mbedtls_writer_init( mbedtls_writer *wr,
                          unsigned char *queue,
                          mbedtls_mps_size_t queue_len )
{
    mbedtls_writer dst =  { .state = MBEDTLS_WRITER_PROVIDING,
          .out   = NULL,
          .queue = queue,
          .out_len   = 0,
          .queue_len = queue_len,
          .committed = 0,
          .end       = 0,
          .queue_next      = 0,
          .queue_remaining = 0 };

    *wr = dst;
}

void mbedtls_writer_free( mbedtls_writer *wr )
{
    mbedtls_writer_init( wr, NULL, 0 );
}

int mbedtls_writer_feed( mbedtls_writer *wr,
                         unsigned char *buf,
                         mbedtls_mps_size_t buf_len )
{
    unsigned char *queue;
    mbedtls_mps_size_t copy_from_queue;
    TRACE_INIT( "writer_feed, buflen %u",
                (unsigned) buf_len );

    /* Feeding is only possible in providing state. */
    MBEDTLS_MPS_STATE_VALIDATE_RAW(
        wr->state == MBEDTLS_WRITER_PROVIDING,
        "Attempt to feed output buffer to writer outside providing mode." );

    /* Check if there is data in the queue pending to be dispatched. */
    queue = wr->queue;
    copy_from_queue = 0;
    if( queue != NULL )
    {
        mbedtls_mps_size_t qa, qr;
        qr = wr->queue_remaining;
        qa = wr->queue_next;
        TRACE( trace_comment, "Queue data pending to be dispatched: %u",
               (unsigned) wr->queue_remaining );

        /* Copy as much data from the queue to
         * the provided buffer as possible. */
        copy_from_queue = qr;
        if( copy_from_queue > buf_len )
            copy_from_queue = buf_len;
        queue += qa;

        if( copy_from_queue != 0 )
            memcpy( buf, queue, copy_from_queue );

        /* Check if, after the last copy, the entire
         * queue has been dispatched. */
        qr -= copy_from_queue;
        if( qr > 0 )
        {
            /* More data waiting in the queue */
            TRACE( trace_comment, "There are %u bytes remaining in the queue.",
                   (unsigned) qr );

            qa += copy_from_queue;
            wr->queue_remaining = qr;
            wr->queue_next = qa;
            RETURN( MBEDTLS_ERR_WRITER_NEED_MORE );
        }

        /* The queue is empty. */
        TRACE( trace_comment, "Queue is empty" );
        wr->queue_next = 0;
        wr->queue_remaining = 0;

        /* NOTE: Currently this returns success if the provided output
         *       buffer is exactly as big as the remaining queue,
         *       in which case there is no space left after the
         *       queue has been copied. Is that intentional? */

    }

    wr->out = buf;
    wr->out_len = buf_len;
    wr->committed = copy_from_queue;
    wr->end = copy_from_queue;
    wr->state = MBEDTLS_WRITER_CONSUMING;
    RETURN( 0 );
}

int mbedtls_writer_reclaim( mbedtls_writer *wr,
                            mbedtls_mps_size_t *olen,
                            mbedtls_mps_size_t *queued,
                            int force )
{
    mbedtls_mps_size_t commit, ol;
    TRACE_INIT( "writer_reclaim" );
    TRACE( trace_comment," * Force reclaim: %u", (unsigned) force );

    /* Check that the writer is in consuming mode. */
    MBEDTLS_MPS_STATE_VALIDATE_RAW(
        wr->state == MBEDTLS_WRITER_CONSUMING,
        "Can't reclaim output buffer outside of consuming mode." );

    /* Check if there's space left unused. */
    commit = wr->committed;
    ol = wr->out_len;

    TRACE( trace_comment, "* Committed: %u Bytes", (unsigned) commit );
    TRACE( trace_comment, "* Buffer length: %u Bytes", (unsigned) ol );

    if( commit <= ol )
    {
        if( olen != NULL )
            *olen = commit;
        if( queued != NULL )
            *queued = 0;

        /* queue_next must be 0 if end <= ol */
        wr->queue_next = 0;

        if( commit < ol && force == 0 )
        {
            wr->end = commit;
            RETURN( MBEDTLS_ERR_WRITER_DATA_LEFT );
        }
    }
    else
    {
        /* The committed parts of the queue that
         * have no overlap with the current outgoing
         * data buffer need to be dispatched on
         * the next call(s) to mbedtls_writer_fetch. */
        wr->queue_remaining = commit - ol;
        /* No need to modify wr->queue_next */

        if( olen != NULL )
            *olen = ol;
    }

    if( queued != NULL )
    {
        mbedtls_mps_size_t qr = wr->queue_remaining;
        TRACE( trace_comment, "%u Bytes are queued for dispatching.",
                (unsigned) wr->queue_remaining );
        *queued = qr;
    }

    wr->end = 0;
    wr->committed = 0;
    wr->out = NULL;
    wr->out_len = 0;
    wr->state = MBEDTLS_WRITER_PROVIDING;
    RETURN( 0 );
}

int mbedtls_writer_bytes_written( mbedtls_writer *wr,
                                  mbedtls_mps_size_t *written )
{
    mbedtls_mps_size_t commit;
    TRACE_INIT( "writer_bytes_written" );

    MBEDTLS_MPS_STATE_VALIDATE_RAW(
        wr->state == MBEDTLS_WRITER_PROVIDING,
        "Attempt to feed output buffer to writer outside providing mode." );

    commit = wr->committed;
    *written = commit;

    RETURN( 0 );
}

int mbedtls_writer_get( mbedtls_writer *wr,
                        mbedtls_mps_size_t desired,
                        unsigned char **buffer,
                        mbedtls_mps_size_t *buflen )
{
    unsigned char *out, *queue;
    mbedtls_mps_size_t end, ol, or, ql, qn, qo;
    TRACE_INIT( "writer_get, desired %u", (unsigned) desired );

    MBEDTLS_MPS_STATE_VALIDATE_RAW( wr->state == MBEDTLS_WRITER_CONSUMING,
                  "Attempt to request write-buffer outside consuming mode." );

    out = wr->out;
    end = wr->end;
    ol = wr->out_len;

    /* Check if we're already serving from the queue */
    if( end > ol )
    {
        TRACE( trace_comment, "already serving from the queue, attempt to continue" );

        ql = wr->queue_len;
        /* If we're serving from the queue, queue_next denotes
         * the size of the overlap between queue and output buffer. */
        qn = wr->queue_next;
        qo = qn + ( end - ol );
        TRACE( trace_comment, "queue overlap %u, queue used %u, queue remaining %u",
               (unsigned) qn, (unsigned) qo, (unsigned) ( ql - qo ) );

        if( ql - qo < desired )
        {
            if( buflen == NULL )
            {
                TRACE( trace_comment, "not enough space remaining in queue" );
                RETURN( MBEDTLS_ERR_WRITER_OUT_OF_DATA );
            }
            desired = ql - qo;
        }

        TRACE( trace_comment, "serving %u bytes from queue", (unsigned) desired );

        queue = wr->queue;
        end += desired;
        wr->end = end;

        *buffer = queue + qo;
        if( buflen != NULL )
            *buflen = desired;

        RETURN( 0 );
    }

    /* We're still serving from the output buffer.
     * Check if there's enough space left in it. */
    or = ol - end;
    TRACE( trace_comment, "%u bytes remaining in output buffer",
           (unsigned) or );
    if( or < desired )
    {
        TRACE( trace_comment, "need %u, but only %u remains in write buffer",
               (unsigned) desired, (unsigned) or );

        queue  = wr->queue;
        ql     = wr->queue_len;

        /* Out buffer is too small. Attempt to serve from queue if it is
         * available and larger than the remaining output buffer. */
        if( queue != NULL && ql > or )
        {
            int overflow;

            if( buflen != NULL && desired > ql )
                desired = ql;

            overflow = ( end + desired < end );
            if( overflow || desired > ql )
            {
                TRACE( trace_comment, "queue present but too small, need %u but only got %u",
                       (unsigned) desired, (unsigned) ql );
                RETURN( MBEDTLS_ERR_WRITER_OUT_OF_DATA );
            }

            /* Queue large enough, transition to serving from queue. */
            end += desired;
            wr->end = end;

            *buffer = queue;
            if( buflen != NULL )
                *buflen = desired;

            /* Remember the overlap between queue and output buffer. */
            wr->queue_next = or;
            TRACE( trace_comment, "served from queue, qo %u",
                   (unsigned) wr->queue_next );

            RETURN( 0 );
        }

        /* No queue present, so serve only what's available
         * in the output buffer, provided the user allows it. */
        if( buflen == NULL )
        {
            TRACE( trace_comment, "no queue present" );
            RETURN( MBEDTLS_ERR_WRITER_OUT_OF_DATA );
        }

        desired = or;
    }

    /* We reach this if the request can be served from the output buffer. */
    out += end;
    end += desired;
    wr->end = end;

    *buffer = out;
    if( buflen != NULL)
        *buflen = desired;

    RETURN( 0 );
}

int mbedtls_writer_commit( mbedtls_writer *wr )
{
    return( mbedtls_writer_commit_partial( wr, 0 ) );
}

int mbedtls_writer_commit_partial( mbedtls_writer *wr,
                                   mbedtls_mps_size_t omit )
{
    mbedtls_mps_size_t to_be_committed, commit, end, queue_overlap;
    mbedtls_mps_size_t out_len, copy_from_queue;
    unsigned char *out, *queue;
    TRACE_INIT( "writer_commit_partial" );
    TRACE( trace_comment, "* Omit %u bytes", (unsigned) omit );

    MBEDTLS_MPS_STATE_VALIDATE_RAW(
        wr->state == MBEDTLS_WRITER_CONSUMING,
        "Attempt to request write-buffer outside consuming mode." );

    out           = wr->out;
    queue_overlap = wr->queue_next;
    commit        = wr->committed;
    end           = wr->end;
    out_len       = wr->out_len;

    if( omit > end - commit )
        RETURN( MBEDTLS_ERR_WRITER_INVALID_ARG );

    to_be_committed = end - omit;

    TRACE( trace_comment, "* Last commit:       %u", (unsigned) commit );
    TRACE( trace_comment, "* End of last fetch: %u", (unsigned) end );
    TRACE( trace_comment, "* New commit:        %u", (unsigned) to_be_committed );

    if( end     > out_len &&
        commit  < out_len &&
        to_be_committed > out_len - queue_overlap )
    {
        /* Copy the beginning of the queue to
         * the end of the outgoing data buffer. */
        copy_from_queue = to_be_committed - ( out_len - queue_overlap );
        if( copy_from_queue > queue_overlap )
            copy_from_queue = queue_overlap;

        TRACE( trace_comment, "copy %u bytes from queue to output buffer",
               (unsigned) copy_from_queue );

        queue = wr->queue;
        out   += out_len - queue_overlap;
        memcpy( out, queue, copy_from_queue );
    }

    if( to_be_committed < out_len )
        wr->queue_next = 0;

    wr->end       = to_be_committed;
    wr->committed = to_be_committed;

    RETURN( 0 );
}

/*
 * Implementation of extended writer
 */

/* TODO: Consider making (some of) these functions inline. */

void mbedtls_writer_init_ext( mbedtls_writer_ext *wr_ext,
                              mbedtls_mps_size_t size )
{
    mbedtls_writer_ext const writer_ext_zero =
        { .wr = NULL,
          .grp_end = { 0 },
          .cur_grp = 0,
          .ofs_fetch = 0,
          .passthrough = MBEDTLS_WRITER_EXT_PASS };

    *wr_ext = writer_ext_zero;
    wr_ext->grp_end[0] = size;
}

void mbedtls_writer_free_ext( mbedtls_writer_ext *wr_ext )
{
    mbedtls_writer_init_ext( wr_ext, 0 );
}

int mbedtls_writer_get_ext( mbedtls_writer_ext *wr_ext,
                            mbedtls_mps_size_t desired,
                            unsigned char **buffer,
                            mbedtls_mps_size_t *buflen )
{
    int ret;
    mbedtls_mps_size_t logic_avail;
    TRACE_INIT( "writer_get_ext: desired %u", (unsigned) desired );

    MBEDTLS_MPS_STATE_VALIDATE_RAW( wr_ext->wr != NULL, "No writer attached" );
    MBEDTLS_MPS_STATE_VALIDATE_RAW(
        wr_ext->passthrough != MBEDTLS_WRITER_EXT_BLOCK,
        "Extended writer is blocked." );

    logic_avail = wr_ext->grp_end[wr_ext->cur_grp] - wr_ext->ofs_fetch;
    TRACE( trace_comment, "desired %u, logic_avail %u",
           (unsigned) desired, (unsigned) logic_avail );
    if( desired > logic_avail )
    {
        TRACE( trace_comment, "bounds violation!" );
        RETURN( MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION );
    }

    ret = mbedtls_writer_get( wr_ext->wr, desired, buffer, buflen );
    if( ret != 0 )
        RETURN( ret );

    if( buflen != NULL )
        desired = *buflen;

    TRACE( trace_comment, "increase fetch offset from %u to %u",
           (unsigned) wr_ext->ofs_fetch,
           (unsigned) ( wr_ext->ofs_fetch + desired )  );

    wr_ext->ofs_fetch += desired;
    RETURN( 0 );
}

int mbedtls_writer_commit_ext( mbedtls_writer_ext *wr )
{
    return( mbedtls_writer_commit_partial_ext( wr, 0 ) );
}

int mbedtls_writer_commit_partial_ext( mbedtls_writer_ext *wr,
                                       mbedtls_mps_size_t omit )
{
    int ret;
    mbedtls_mps_size_t ofs_fetch, ofs_commit;
    TRACE_INIT( "writer_commit_partial_ext, omit %u",
                (unsigned) omit );

    MBEDTLS_MPS_STATE_VALIDATE_RAW( wr->wr != NULL, "No writer attached" );
    MBEDTLS_MPS_STATE_VALIDATE_RAW(
        wr->passthrough != MBEDTLS_WRITER_EXT_BLOCK,
        "Extended writer is blocked." );

    ofs_fetch  = wr->ofs_fetch;
    ofs_commit = wr->ofs_commit;

    if( omit > ofs_fetch - ofs_commit )
    {
        TRACE( trace_error, "Try to omit %u bytes from commit, but only %u are uncommitted.",
               (unsigned) omit, (unsigned)( ofs_fetch - ofs_commit ) );
        RETURN( MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION );
    }

    ofs_commit = ofs_fetch - omit;

    if( wr->passthrough == MBEDTLS_WRITER_EXT_PASS )
    {
        TRACE( trace_comment, "Forward commit to underlying writer" );
        ret = mbedtls_writer_commit_partial( wr->wr, omit );
        if( ret != 0 )
            RETURN( ret );

        ofs_fetch = ofs_commit;
    }

    if( wr->passthrough == MBEDTLS_WRITER_EXT_HOLD &&
        omit > 0 )
    {
        TRACE( trace_comment, "Partial commit, blocking writer" );
        wr->passthrough = MBEDTLS_WRITER_EXT_BLOCK;
    }

    wr->ofs_fetch  = ofs_fetch;
    wr->ofs_commit = ofs_commit;
    RETURN( 0 );
}

int mbedtls_writer_group_open( mbedtls_writer_ext *wr_ext,
                               mbedtls_mps_size_t group_size )
{
    /* Check how much space is left in the current group */
    mbedtls_mps_size_t const logic_avail =
        wr_ext->grp_end[wr_ext->cur_grp] - wr_ext->ofs_fetch;
    TRACE_INIT( "writer_group_open, size %u", (unsigned) group_size );

    if( wr_ext->cur_grp >= MBEDTLS_WRITER_MAX_GROUPS - 1 )
        RETURN( MBEDTLS_ERR_WRITER_TOO_MANY_GROUPS );

    /* Make sure the new group doesn't exceed the present one */
    if( logic_avail < group_size )
        RETURN( MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION );

    /* Add new group */
    wr_ext->cur_grp++;
    wr_ext->grp_end[wr_ext->cur_grp] = wr_ext->ofs_fetch + group_size;

    RETURN( 0 );
}

int mbedtls_writer_group_close( mbedtls_writer_ext *wr_ext )
{
    /* Check how much space is left in the current group */
    mbedtls_mps_size_t const logic_avail =
        wr_ext->grp_end[wr_ext->cur_grp] - wr_ext->ofs_fetch;
    TRACE_INIT( "writer_group_close" );

    /* Ensure that the group is fully exhausted */
    if( logic_avail != 0 )
        RETURN( MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION );

    if( wr_ext->cur_grp > 0 )
        wr_ext->cur_grp--;

    RETURN( 0 );
}

int mbedtls_writer_attach( mbedtls_writer_ext *wr_ext,
                           mbedtls_writer *wr,
                           int pass )
{
    TRACE_INIT( "mbedtls_writer_attach" );
    MBEDTLS_MPS_STATE_VALIDATE_RAW( wr_ext->wr == NULL, "Writer attached" );

    wr_ext->passthrough = pass;
    wr_ext->wr = wr;

    RETURN( 0 );
}

int mbedtls_writer_detach( mbedtls_writer_ext *wr_ext,
                           mbedtls_mps_size_t *committed,
                           mbedtls_mps_size_t *uncommitted )
{
    TRACE_INIT( "writer_check_detach" );
    MBEDTLS_MPS_STATE_VALIDATE_RAW( wr_ext->wr != NULL, "No writer attached" );

    if( uncommitted != NULL )
    {
        *uncommitted = wr_ext->ofs_fetch - wr_ext->ofs_commit;
        TRACE( trace_comment, "Uncommitted: %u",
               (unsigned) *uncommitted );
    }
    if( committed != NULL )
    {
        *committed = wr_ext->ofs_commit;
        TRACE( trace_comment, "Committed: %u",
               (unsigned) *committed );
    }

    wr_ext->ofs_fetch = wr_ext->ofs_commit;
    wr_ext->wr = NULL;

    RETURN( 0 );
}

int mbedtls_writer_check_done( mbedtls_writer_ext *wr_ext )
{
    TRACE_INIT( "writer_check_done" );
    TRACE( trace_comment, "* Commit: %u", (unsigned) wr_ext->ofs_commit );
    TRACE( trace_comment, "* Group end: %u", (unsigned) wr_ext->grp_end[0] );

    if( wr_ext->cur_grp > 0 )
    {
        TRACE( trace_comment, "cur_grp > 0" );
        RETURN( MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION );
    }

    if( wr_ext->grp_end[0] != MBEDTLS_MPS_SIZE_MAX &&
        wr_ext->ofs_commit != wr_ext->grp_end[0] )
    {
        RETURN( MBEDTLS_ERR_WRITER_BOUNDS_VIOLATION );
    }

    RETURN( 0 );
}

#endif /* MBEDTLS_MPS_SEPARATE_LAYERS) ||
          MBEDTLS_MPS_TOP_TRANSLATION_UNIT */
