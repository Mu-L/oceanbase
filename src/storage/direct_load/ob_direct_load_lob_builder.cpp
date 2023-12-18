/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX STORAGE

#include "storage/direct_load/ob_direct_load_lob_builder.h"
#include "share/stat/ob_stat_define.h"
#include "share/table/ob_table_load_define.h"
#include "storage/direct_load/ob_direct_load_insert_table_ctx.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace blocksstable;
using namespace share;

/**
 * ObDirectLoadLobBuildParam
 */

ObDirectLoadLobBuildParam::ObDirectLoadLobBuildParam()
  : insert_table_ctx_(nullptr), lob_column_cnt_(0)
{
}

ObDirectLoadLobBuildParam::~ObDirectLoadLobBuildParam()
{
}

bool ObDirectLoadLobBuildParam::is_valid() const
{
  return tablet_id_.is_valid() && nullptr != insert_table_ctx_;
}

/**
 * ObDirectLoadLobBuilder
 */

ObDirectLoadLobBuilder::ObDirectLoadLobBuilder()
  : insert_tablet_ctx_(nullptr),
    current_lob_slice_id_(0),
    is_closed_(false),
    is_inited_(false)
{
}

ObDirectLoadLobBuilder::~ObDirectLoadLobBuilder()
{
}

int ObDirectLoadLobBuilder::init(const ObDirectLoadLobBuildParam &param)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadLobBuilder init twice", KR(ret), KP(this));
  } else if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(param));
  } else {
    param_ = param;
    if (OB_FAIL(param_.insert_table_ctx_->get_tablet_context(
                 param_.tablet_id_, insert_tablet_ctx_))) {
      LOG_WARN("fail to get tablet context", KR(ret));
    } else if (OB_FAIL(init_sstable_slice_ctx())) {
      LOG_WARN("fail to init sstable slice ctx", KR(ret));
    } else {
      lob_tablet_id_ = insert_tablet_ctx_->get_tablet_id();
      is_inited_ = true;
    }
  }
  return ret;
}

int ObDirectLoadLobBuilder::init_sstable_slice_ctx()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(insert_tablet_ctx_->get_lob_write_ctx(write_ctx_))) {
    LOG_WARN("fail to get write ctx", KR(ret));
  } else if (OB_FAIL(insert_tablet_ctx_->open_lob_sstable_slice(
               write_ctx_.start_seq_,
               current_lob_slice_id_))) {
    LOG_WARN("fail to construct sstable slice", KR(ret));
  }
  return ret;
}

int ObDirectLoadLobBuilder::switch_sstable_slice()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(insert_tablet_ctx_->close_lob_sstable_slice(current_lob_slice_id_))) {
    LOG_WARN("fail to close sstable slice builder", KR(ret));
  } else  if (OB_FAIL(init_sstable_slice_ctx())) {
    LOG_WARN("fail to init sstable slice ctx", KR(ret));
  }
  return ret;
}

int ObDirectLoadLobBuilder::append_lob(ObIAllocator &allocator, blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadLobBuilder not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(is_closed_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob builder is closed", KR(ret));
  } else {
    if (write_ctx_.pk_interval_.remain_count() < param_.lob_column_cnt_) {
       if (OB_FAIL(switch_sstable_slice())) {
        LOG_WARN("fail to switch sstable slice", KR(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(insert_tablet_ctx_->fill_lob_sstable_slice(allocator, current_lob_slice_id_,
          write_ctx_.pk_interval_, datum_row))) {
        LOG_WARN("fill lob sstable slice failed", K(ret), KP(insert_tablet_ctx_), K(current_lob_slice_id_), K(write_ctx_.pk_interval_), K(datum_row));
      }
    }
  }
  return ret;
}

int ObDirectLoadLobBuilder::close()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadLobBuilder not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(is_closed_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet lob builder is closed", KR(ret));
  } else {
    if (OB_FAIL(insert_tablet_ctx_->close_lob_sstable_slice(current_lob_slice_id_))) {
      LOG_WARN("fail to close sstable slice ", KR(ret));
    } else {
      current_lob_slice_id_ = 0;
      is_closed_ = true;
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
