// Copyright (c) 2024 OceanBase
// OceanBase is licensed under Mulan PubL v2.
// You can use this software according to the terms and conditions of the Mulan PubL v2.
// You may obtain a copy of Mulan PubL v2 at:
//          http://license.coscl.org.cn/MulanPubL-2.0
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
// See the Mulan PubL v2 for more details.

#ifndef OCEANBASE_STORAGE_OB_MVIEW_COMPACTION_UTIL_
#define OCEANBASE_STORAGE_OB_MVIEW_COMPACTION_UTIL_

#include "common/ob_tablet_id.h"
#include "common/object/ob_object.h"
#include "lib/string/ob_sql_string.h"
#include "share/ob_table_range.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObTableSchema;
struct ObColDesc;
}
}

namespace common
{
namespace sqlclient
{
class ObISQLConnection;
}
}
namespace sql
{
class ObSQLSessionInfo;
class ObFreeSessionCtx;
}
namespace storage
{
class ObRowkeyReadInfo;
}
namespace blocksstable
{
struct ObDatumRange;
}
namespace compaction
{

struct ObMergeParameter;
class ObPartitionMergeIter;

enum class ObMviewMergeIterType
{
  MVIEW_INSERT = 0,
  MVIEW_DELETE,
  MVIEW_REPLACE
};

struct ObMviewMergeSQL
{
  OB_INLINE bool is_delete() const
  {
    return ObMviewMergeIterType::MVIEW_DELETE == type_;
  }
  OB_INLINE bool is_replace() const
  {
    return ObMviewMergeIterType::MVIEW_REPLACE == type_;
  }
  TO_STRING_KV(K_(type), K_(sql));
  ObMviewMergeIterType type_;
  ObSqlString sql_;
};

struct ObMviewMergeParameter
{
  static const int64_t MAX_REFRESH_SQL_COUNT = 4;
  static const int64_t REFRESH_SQL_COUNT_V0 = MAX_REFRESH_SQL_COUNT;
  static const int64_t REFRESH_SQL_COUNT_V1 = 2;
  ObMviewMergeParameter();
  ~ObMviewMergeParameter();
  int init(const ObMergeParameter &merge_param);
  OB_INLINE bool is_valid() const
  {
    return database_id_ > 0 && mview_id_ > 0 && container_table_id_ > 0 && container_tablet_id_.is_valid() &&
           schema_version_ > 0 && refresh_scn_range_.is_valid();
  }
  OB_INLINE bool is_refresh_sql_v0() const
  {
    return REFRESH_SQL_COUNT_V0 == refresh_sql_count_;
  }
  DECLARE_TO_STRING;
  uint64_t database_id_;
  uint64_t mview_id_;
  uint64_t container_table_id_;
  ObTabletID container_tablet_id_;
  int64_t schema_version_;
  share::ObScnRange refresh_scn_range_; // (last_refresh_scn, current_refresh_scn]
  int64_t refresh_sql_count_;
  ObMviewMergeSQL refresh_sqls_[MAX_REFRESH_SQL_COUNT];
};

class ObMviewCompactionHelper
{
public:
  static const int64_t REFRESH_SQL_TIMEOUT_US = 604800000000;
  static int get_mview_id_from_container_table(const uint64_t container_table_id, uint64_t &mview_id);
  static int generate_mview_refresh_sql(
      sql::ObSQLSessionInfo *session,
      share::schema::ObSchemaGetterGuard &schema_guard,
      const share::schema::ObTableSchema *table_schema,
      const blocksstable::ObDatumRange &merge_range,
      const storage::ObRowkeyReadInfo *rowkey_read_info,
      ObMviewMergeParameter &mview_param);
  static int create_inner_session(
      const bool is_oracle_mode,
      const uint64_t database_id,
      sql::ObFreeSessionCtx &free_session_ctx,
      sql::ObSQLSessionInfo *&session);
  static void release_inner_session(sql::ObFreeSessionCtx &free_session_ctx, sql::ObSQLSessionInfo *&session);
  static int create_inner_connection(sql::ObSQLSessionInfo *session, common::sqlclient::ObISQLConnection *&connection);
  static void release_inner_connection(common::sqlclient::ObISQLConnection *&connection);
  static int set_params_to_session(sql::ObSQLSessionInfo *session);
private:
  static int convert_datum_range(
      common::ObIAllocator &allocator,
      const storage::ObRowkeyReadInfo *rowkey_read_info,
      const blocksstable::ObDatumRange &merge_range,
      ObNewRange &sql_range);
};

}
}

#endif