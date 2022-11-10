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
#include "ob_ls_meta.h"
#include "storage/slog/ob_storage_logger.h"
#include "storage/slog/ob_storage_log.h"
#include "storage/slog/ob_storage_log_replayer.h"

namespace oceanbase
{
using namespace common;
using namespace palf;
using namespace logservice;
using namespace share;
namespace storage
{

typedef common::ObFunction<int(ObLSMeta &)> WriteSlog;
WriteSlog ObLSMeta::write_slog_ = [](ObLSMeta &ls_meta) {
  int ret = OB_SUCCESS;
  ObLSMetaLog slog_entry(ls_meta);
  ObStorageLogParam log_param;
  log_param.data_ = &slog_entry;
  log_param.cmd_ = ObIRedoModule::gen_cmd(ObRedoLogMainType::OB_REDO_LOG_TENANT_STORAGE,
                                            ObRedoLogSubType::OB_REDO_LOG_UPDATE_LS);
  ObStorageLogger *slogger = nullptr;
  if (OB_ISNULL(slogger = MTL(ObStorageLogger *))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get_log_service failed", K(ret));
  } else if (OB_FAIL(slogger->write_log(log_param))) {
    LOG_WARN("fail to write ls meta slog", K(log_param), K(ret));
  }
  return ret;
};

ObLSMeta::ObLSMeta()
  : lock_(),
    tenant_id_(OB_INVALID_TENANT_ID),
    ls_id_(),
    replica_type_(REPLICA_TYPE_MAX),
    ls_create_status_(ObInnerLSStatus::CREATING),
    clog_checkpoint_ts_(0),
    clog_base_lsn_(PALF_INITIAL_LSN_VAL),
    rebuild_seq_(0),
    migration_status_(ObMigrationStatus::OB_MIGRATION_STATUS_MAX),
    gc_state_(LSGCState::INVALID_LS_GC_STATE),
    offline_ts_ns_(OB_INVALID_TIMESTAMP),
    restore_status_(ObLSRestoreStatus::LS_RESTORE_STATUS_MAX),
    replayable_point_(OB_INVALID_TIMESTAMP),
    tablet_change_checkpoint_ts_(0),
    all_id_meta_(),
    saved_info_()
{
}

ObLSMeta::ObLSMeta(const ObLSMeta &ls_meta)
  : lock_(),
    tenant_id_(ls_meta.tenant_id_),
    ls_id_(ls_meta.ls_id_),
    replica_type_(ls_meta.replica_type_),
    ls_create_status_(ls_meta.ls_create_status_),
    clog_checkpoint_ts_(ls_meta.clog_checkpoint_ts_),
    clog_base_lsn_(ls_meta.clog_base_lsn_),
    rebuild_seq_(ls_meta.rebuild_seq_),
    migration_status_(ls_meta.migration_status_),
    gc_state_(ls_meta.gc_state_),
    offline_ts_ns_(ls_meta.offline_ts_ns_),
    restore_status_(ls_meta.restore_status_),
    replayable_point_(ls_meta.replayable_point_),
    tablet_change_checkpoint_ts_(ls_meta.tablet_change_checkpoint_ts_),
    saved_info_(ls_meta.saved_info_)
{
  all_id_meta_.update_all_id_meta(ls_meta.all_id_meta_);
}

ObLSMeta &ObLSMeta::operator=(const ObLSMeta &other)
{
  ObSpinLockTimeGuard guard(other.lock_);
  if (this != &other) {
    tenant_id_ = other.tenant_id_;
    ls_id_ = other.ls_id_;
    replica_type_ = other.replica_type_;
    ls_create_status_ = other.ls_create_status_;
    rebuild_seq_ = other.rebuild_seq_;
    migration_status_ = other.migration_status_;
    clog_base_lsn_ = other.clog_base_lsn_;
    clog_checkpoint_ts_ = other.clog_checkpoint_ts_;
    gc_state_ = other.gc_state_;
    offline_ts_ns_ = other.offline_ts_ns_;
    restore_status_ = other.restore_status_;
    replayable_point_ = other.replayable_point_;
    tablet_change_checkpoint_ts_ = other.tablet_change_checkpoint_ts_;
    all_id_meta_.update_all_id_meta(other.all_id_meta_);
    saved_info_ = other.saved_info_;
  }
  return *this;
}

void ObLSMeta::reset()
{
  ObSpinLockTimeGuard guard(lock_);
  tenant_id_ = OB_INVALID_TENANT_ID;
  ls_id_.reset();
  replica_type_ = REPLICA_TYPE_MAX;
  clog_base_lsn_.reset();
  clog_checkpoint_ts_ = 0;
  rebuild_seq_ = 0;
  migration_status_ = ObMigrationStatus::OB_MIGRATION_STATUS_MAX;
  gc_state_ = LSGCState::INVALID_LS_GC_STATE;
  offline_ts_ns_ = OB_INVALID_TIMESTAMP;
  restore_status_ = ObLSRestoreStatus::LS_RESTORE_STATUS_MAX;
  replayable_point_ = OB_INVALID_TIMESTAMP;
  tablet_change_checkpoint_ts_ = 0;
  saved_info_.reset();
}

LSN &ObLSMeta::get_clog_base_lsn()
{
  ObSpinLockTimeGuard guard(lock_);
  return clog_base_lsn_;
}

int64_t ObLSMeta::get_clog_checkpoint_ts() const
{
  ObSpinLockTimeGuard guard(lock_);
 	return clog_checkpoint_ts_;
}

int ObLSMeta::set_clog_checkpoint(const LSN &clog_checkpoint_lsn,
                                  const int64_t clog_checkpoint_ts,
                                  const bool write_slog)
{
  ObSpinLockTimeGuard guard(lock_);
  ObLSMeta tmp(*this);
  tmp.clog_base_lsn_ = clog_checkpoint_lsn;
  tmp.clog_checkpoint_ts_ = clog_checkpoint_ts;

  int ret = OB_SUCCESS;
  if (write_slog) {
    if (OB_FAIL(write_slog_(tmp))) {
      LOG_WARN("clog_checkpoint write slog failed", K(ret));
    }
  }

  clog_base_lsn_ = clog_checkpoint_lsn;
  clog_checkpoint_ts_ = clog_checkpoint_ts;

  return ret;
}

int64_t ObLSMeta::get_tablet_change_checkpoint_ts() const
{
  ObSpinLockTimeGuard guard(lock_);
 	return tablet_change_checkpoint_ts_;
}

int ObLSMeta::set_tablet_change_checkpoint_ts(const int64_t tablet_change_checkpoint_ts)
{
  ObSpinLockTimeGuard guard(lock_);
  int ret = OB_SUCCESS;
  if (tablet_change_checkpoint_ts_ > tablet_change_checkpoint_ts) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tablet_change_checkpoint_ts is small", KR(ret), K(tablet_change_checkpoint_ts), K_(tablet_change_checkpoint_ts));
  } else {
    ObLSMeta tmp(*this);
    tmp.tablet_change_checkpoint_ts_ = tablet_change_checkpoint_ts;

    if (OB_FAIL(write_slog_(tmp))) {
      LOG_WARN("clog_checkpoint write slog failed", K(ret));
    } else {
      tablet_change_checkpoint_ts_ = tablet_change_checkpoint_ts;
    }
  }

  return ret;
}

bool ObLSMeta::is_valid() const
{
  return is_valid_id(tenant_id_)
      && ls_id_.is_valid()
      && REPLICA_TYPE_MAX != replica_type_
      && OB_MIGRATION_STATUS_MAX != migration_status_
      && ObGCHandler::is_valid_ls_gc_state(gc_state_)
      && restore_status_.is_valid();
}

int64_t ObLSMeta::get_rebuild_seq() const
{
  ObSpinLockTimeGuard guard(lock_);
  return rebuild_seq_;
}

int ObLSMeta::set_migration_status(const ObMigrationStatus &migration_status,
                                   const bool write_slog)
{
  int ret = OB_SUCCESS;
  bool can_change = false;
  if (!ObMigrationStatusHelper::is_valid(migration_status)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObSpinLockTimeGuard guard(lock_);
    if (migration_status_ == migration_status) {
      //do nothing
    } else if (ObMigrationStatusHelper::check_can_change_status(migration_status_, migration_status, can_change)) {
      LOG_WARN("failed to check can change stauts", K(ret), K(migration_status_), K(migration_status));
    } else {
      ObLSMeta tmp(*this);
      tmp.migration_status_ = migration_status;
      if (write_slog && OB_FAIL(write_slog_(tmp))) {
        LOG_WARN("migration_status write slog failed", K(ret));
      } else {
        migration_status_ = migration_status;
      }
    }
  }
  return ret;
}

int ObLSMeta::get_migration_status(ObMigrationStatus &migration_status) const
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid, cannot get migration status", K(ret), K(*this));
  } else {
    migration_status = migration_status_;
  }
  return ret;
}

int ObLSMeta::set_gc_state(const logservice::LSGCState &gc_state)
{
  int ret = OB_SUCCESS;
  if (!ObGCHandler::is_valid_ls_gc_state(gc_state)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("gc_state invalid", K(ret), K(gc_state));
  } else {
    ObSpinLockTimeGuard guard(lock_);
    ObLSMeta tmp(*this);
    tmp.gc_state_ = gc_state;
    if (OB_FAIL(write_slog_(tmp))) {
      LOG_WARN("gc_state write slog failed", K(ret));
    } else {
      gc_state_ = gc_state;
    }
  }
  return ret;
}

int ObLSMeta::get_gc_state(logservice::LSGCState &gc_state)
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid, cannot get_gc_state", K(ret), K(*this));
  } else {
    gc_state = gc_state_;
  }
  return ret;
}

int ObLSMeta::set_offline_ts_ns(const int64_t offline_ts_ns)
{
  // 不主动写slog
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  offline_ts_ns_ = offline_ts_ns;
  return ret;
}

int ObLSMeta::get_offline_ts_ns(int64_t &offline_ts_ns)
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid, cannot get_offline_ts_ns", K(ret), K(*this));
  } else {
    offline_ts_ns = offline_ts_ns_;
  }
  return ret;
}

int ObLSMeta::set_restore_status(const ObLSRestoreStatus &restore_status)
{
  int ret = OB_SUCCESS;
  if (!restore_status.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid restore status", K(ret), K(restore_status_), K(restore_status));
  } else {
    ObSpinLockTimeGuard guard(lock_);
    if (restore_status_ == restore_status) {
      //do nothing
    } else {
      ObLSMeta tmp(*this);
      tmp.restore_status_ = restore_status;
      if (OB_FAIL(write_slog_(tmp))) {
        LOG_WARN("restore_status write slog failed", K(ret));
      } else {
        restore_status_ = restore_status;
      }
    }
  }
  return ret;
}

int ObLSMeta::get_restore_status(ObLSRestoreStatus &restore_status) const
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid, cannot get restore status", K(ret), K(*this));
  } else {
    restore_status = restore_status_;
  }
  return ret;
}

int ObLSMeta::update_ls_replayable_point(const int64_t replayable_point)
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  if (replayable_point < replayable_point_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("replayable_point invalid", K(ret), K(replayable_point), K(replayable_point_));
  } else if (replayable_point_ == replayable_point) {
    // do nothing
  } else {
    ObLSMeta tmp(*this);
    tmp.replayable_point_ = replayable_point;
    if (OB_FAIL(write_slog_(tmp))) {
      LOG_WARN("replayable_point_ write slog failed", K(ret));
    } else {
      replayable_point_ = replayable_point;
    }
  }
  return ret;
}

int ObLSMeta::get_ls_replayable_point(int64_t &replayable_point)
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid, cannot get_gc_state", K(ret), K(*this));
  } else {
    replayable_point = replayable_point_;
  }
  return ret;
}

//This interface for ha. Add parameters should check meta value need to update from src
int ObLSMeta::update_ls_meta(
    const bool update_restore_status,
    const ObLSMeta &src_ls_meta)
{
  int ret = OB_SUCCESS;
  ObLSRestoreStatus ls_restore_status;

  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls meta is not valid, cannot update", K(ret), K(*this));
  } else if (!src_ls_meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("update ls meta get invalid argument", K(ret), K(src_ls_meta));
  } else if (update_restore_status
      && OB_FAIL(src_ls_meta.get_restore_status(ls_restore_status))) {
    LOG_WARN("failed to get restore status", K(ret), K(src_ls_meta));
  } else {
    {
      ObSpinLockTimeGuard guard(lock_);
      ObLSMeta tmp(*this);
      tmp.clog_base_lsn_ = src_ls_meta.clog_base_lsn_;
      tmp.clog_checkpoint_ts_ = src_ls_meta.clog_checkpoint_ts_;
      tmp.replayable_point_ = src_ls_meta.replayable_point_;
      tmp.tablet_change_checkpoint_ts_ = src_ls_meta.tablet_change_checkpoint_ts_;
      tmp.rebuild_seq_++;
      if (update_restore_status) {
        tmp.restore_status_ = ls_restore_status;
      }
      guard.click();
      tmp.all_id_meta_.update_all_id_meta(src_ls_meta.all_id_meta_);
      if (tmp.clog_checkpoint_ts_ < clog_checkpoint_ts_) {
  // TODO: now do not allow clog checkpoint ts rollback, may support it in 4.1
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("do not allow clog checkpoint ts rollback", K(ret), K(src_ls_meta), KPC(this));
      } else if (OB_FAIL(write_slog_(tmp))) {
        LOG_WARN("clog_checkpoint write slog failed", K(ret));
      } else {
        guard.click();
        clog_base_lsn_ = src_ls_meta.clog_base_lsn_;
        clog_checkpoint_ts_ = src_ls_meta.clog_checkpoint_ts_;
        replayable_point_ = src_ls_meta.replayable_point_;
        tablet_change_checkpoint_ts_ = src_ls_meta.tablet_change_checkpoint_ts_;

        if (update_restore_status) {
          restore_status_ = ls_restore_status;
        }
        all_id_meta_.update_all_id_meta(src_ls_meta.all_id_meta_);
      }
      LOG_INFO("update ls meta", K(ret), K(tmp), K(src_ls_meta), K(*this));
    }
    if (OB_SUCC(ret) && IDS_LS == ls_id_) {
      if (OB_FAIL(all_id_meta_.update_id_service())) {
        LOG_WARN("update id service with ls meta fail", K(ret), K(*this));
      }
    }
  }
  return ret;
}

int ObLSMeta::set_ls_rebuild()
{
  int ret = OB_SUCCESS;
  const ObMigrationStatus change_status = ObMigrationStatus::OB_MIGRATION_STATUS_REBUILD;
  bool can_change = false;

  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls meta is not valid, cannot update", K(ret), K(*this));
  } else {
    ObSpinLockTimeGuard guard(lock_);
    ObLSMeta tmp(*this);

    if (OB_FAIL(ObMigrationStatusHelper::check_can_change_status(tmp.migration_status_, change_status, can_change))) {
      LOG_WARN("failed to check can change status", K(ret), K(migration_status_), K(change_status));
    } else if (!can_change) {
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("ls can not change to rebuild status", K(ret), K(tmp), K(change_status));
    } else {
      tmp.migration_status_ = change_status;
      tmp.rebuild_seq_++;
      if (OB_FAIL(write_slog_(tmp))) {
        LOG_WARN("clog_checkpoint write slog failed", K(ret));
      } else {
        migration_status_ = change_status;
        rebuild_seq_ = tmp.rebuild_seq_;
      }
    }
  }
  return ret;
}

int ObLSMeta::check_valid_for_backup()
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid", K(ret), KPC(this));
  } else if (!restore_status_.is_restore_none()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore status is not none", K(ret), KPC(this));
  } else if (OB_MIGRATION_STATUS_NONE != migration_status_) {
    ret = OB_REPLICA_CANNOT_BACKUP;
    LOG_WARN("ls replica not valid for backup", K(ret), KPC(this));
  }
  return ret;
}

int ObLSMeta::get_saved_info(ObLSSavedInfo &saved_info)
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log stream meta is not valid, cannot get_offline_ts_ns", K(ret), K(*this));
  } else {
    saved_info = saved_info_;
  }
  return ret;
}

int ObLSMeta::set_saved_info(const ObLSSavedInfo &saved_info)
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls meta is not valid, cannot update", K(ret), K(*this));
  } else if (!saved_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("set saved info get invalid argument", K(ret), K(saved_info));
  } else {
    ObLSMeta tmp(*this);
    tmp.saved_info_ = saved_info;
    if (OB_FAIL(write_slog_(tmp))) {
      LOG_WARN("clog_checkpoint write slog failed", K(ret));
    } else {
      saved_info_ = saved_info;
    }
  }
  return ret;
}

int ObLSMeta::build_saved_info()
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  ObLSSavedInfo saved_info;

  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls meta is not valid, cannot update", K(ret), K(*this));
  } else if (!saved_info_.is_empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("saved info is not empty, can not build saved info", K(ret), K(*this));
  } else {
    saved_info.clog_checkpoint_ts_ = clog_checkpoint_ts_;
    saved_info.clog_base_lsn_ = clog_base_lsn_;
    saved_info.tablet_change_checkpoint_ts_ = tablet_change_checkpoint_ts_;
    ObLSMeta tmp(*this);
    tmp.saved_info_ = saved_info;
    if (OB_FAIL(write_slog_(tmp))) {
      LOG_WARN("clog_checkpoint write slog failed", K(ret));
    } else {
      saved_info_ = saved_info;
    }
  }
  return ret;
}

int ObLSMeta::clear_saved_info()
{
  int ret = OB_SUCCESS;
  ObSpinLockTimeGuard guard(lock_);
  ObLSSavedInfo saved_info;

  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls meta is not valid, cannot update", K(ret), K(*this));
  } else {
    saved_info.reset();
    ObLSMeta tmp(*this);
    tmp.saved_info_ = saved_info;
    if (OB_FAIL(write_slog_(tmp))) {
      LOG_WARN("clog_checkpoint write slog failed", K(ret));
    } else {
      saved_info_ = saved_info;
    }
  }
  return ret;
}

int ObLSMeta::init(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const ObReplicaType &replica_type,
    const ObMigrationStatus &migration_status,
    const share::ObLSRestoreStatus &restore_status,
    const int64_t create_scn)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_ID == tenant_id || !ls_id.is_valid()
      || !ObReplicaTypeCheck::is_replica_type_valid(replica_type)
      || !ObMigrationStatusHelper::is_valid(migration_status)
      || !restore_status.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init ls meta get invalid argument", K(ret), K(tenant_id), K(ls_id),
             K(replica_type), K(migration_status), K(restore_status));
  } else {
    tenant_id_ = tenant_id;
    ls_id_ = ls_id;
    replica_type_ = replica_type;
    ls_create_status_ = ObInnerLSStatus::CREATING;
    clog_checkpoint_ts_ = create_scn;
    clog_base_lsn_.val_ = PALF_INITIAL_LSN_VAL;
    rebuild_seq_ = 0;
    migration_status_ = migration_status;
    gc_state_ = LSGCState::NORMAL;
    restore_status_ = restore_status;
  }
  return ret;
}

void ObLSMeta::set_write_slog_func_(WriteSlog write_slog)
{
  write_slog_ = write_slog;
}

int ObLSMeta::update_id_meta(const int64_t service_type,
                             const int64_t limited_id,
                             const int64_t latest_log_ts,
                             const bool write_slog)
{
  int ret = OB_SUCCESS;

  ObSpinLockTimeGuard guard(lock_);
  all_id_meta_.update_id_meta(service_type, limited_id, latest_log_ts);
  guard.click();
  if (write_slog) {
    if (OB_FAIL(write_slog_(*this))) {
      LOG_WARN("id service flush write slog failed", K(ret));
    }
  }
  LOG_INFO("update id meta", K(ret), K(service_type), K(limited_id), K(latest_log_ts),
                            K(*this));

  return ret;
}

int ObLSMeta::get_migration_and_restore_status(
    ObMigrationStatus &migration_status,
    share::ObLSRestoreStatus &ls_restore_status)
{
  int ret = OB_SUCCESS;
  migration_status = ObMigrationStatus::OB_MIGRATION_STATUS_MAX;
  ls_restore_status = ObLSRestoreStatus::LS_RESTORE_STATUS_MAX;

  ObSpinLockTimeGuard guard(lock_);
  if (!is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls meta is not valid, cannot get", K(ret), K(*this));
  } else {
    migration_status = migration_status_;
    ls_restore_status = restore_status_;
  }
  return ret;
}

ObLSMeta::ObSpinLockTimeGuard::ObSpinLockTimeGuard(common::ObSpinLock &lock,
                                                   const int64_t warn_threshold)
  : time_guard_("ls_meta", warn_threshold),
    lock_guard_(lock)
{
  time_guard_.click("after lock");
}

OB_SERIALIZE_MEMBER(ObLSMeta,
                    tenant_id_,
                    ls_id_,
                    replica_type_,
                    ls_create_status_,
                    clog_checkpoint_ts_,
                    clog_base_lsn_,
                    rebuild_seq_,
                    migration_status_,
                    gc_state_,
                    offline_ts_ns_,
                    restore_status_,
                    replayable_point_,
                    tablet_change_checkpoint_ts_,
                    all_id_meta_,
                    saved_info_);

}
}
