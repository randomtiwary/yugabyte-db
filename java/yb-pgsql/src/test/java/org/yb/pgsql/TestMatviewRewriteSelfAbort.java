// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

import org.yb.minicluster.MiniYBClusterBuilder;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;
import org.yb.util.SkipOnTSAN;

import java.util.concurrent.ThreadLocalRandom;

/**
 * Reproduces (or stresses) self-abort after CREATE MATERIALIZED VIEW + REFRESH in one
 * transaction with savepoints under transactional DDL.
 *
 * <p>Failure signature:
 * <pre>
 * ERROR: current transaction is expired or aborted ...
 * DETAIL: Unknown transaction, could be recently aborted: ...
 * </pre>
 *
 * <p>Why this does not show up often in the wild:
 * <ul>
 *   <li>Uncommon SQL pattern: CREATE a matview and REFRESH that same matview before
 *       COMMIT/ROLLBACK (REFRESH rewrites DocDB: new table + drop old).</li>
 *   <li>Needs preview DDL savepoint support ({@code ysql_yb_enable_ddl_savepoint_support}).</li>
 *   <li>Racey: tablet deletion of rewritten DocDB tables can AbortActiveTransactions the
 *       still-running client txn when exclude_aborting_transaction_id is missing on the
 *       full-txn cleanup path. Related coverage in {@code yb.orig.ddl_savepoint} is often green
 *       for the same reason the production race is intermittent.</li>
 * </ul>
 *
 * <p>Stress with many iterations:
 * <pre>
 * ./yb_build.sh release --java-test 'org.yb.pgsql.TestMatviewRewriteSelfAbort' -n 50 --tp 1
 * </pre>
 */
@SkipOnTSAN
@RunWith(value = YBTestRunner.class)
public class TestMatviewRewriteSelfAbort extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return getPerfMaxRuntime(500, 1000, 1200, 1200, 1200);
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.numMasters(1);
    builder.enablePgTransactions(true);
    builder.addCommonTServerFlag("ysql_log_statement", "all");
    builder.addCommonTServerFlag("ysql_yb_ddl_transaction_block_enabled", "true");
    builder.addCommonTServerFlag("enable_object_locking_for_table_locks", "true");
    builder.addCommonTServerFlag("ysql_yb_enable_ddl_savepoint_support", "true");
    builder.addCommonTServerFlag("ysql_bypass_anonymous_savepoint_ddl_check", "false");
    builder.addCommonTServerFlag(
        "allowed_preview_flags_csv",
        "ysql_yb_enable_ddl_savepoint_support,ysql_yb_enable_new_relation_fastpath_write_in_txn_"
            + "blocks");
    if (ThreadLocalRandom.current().nextBoolean()) {
      builder.addCommonTServerFlag(
          "ysql_yb_enable_new_relation_fastpath_write_in_txn_blocks", "true");
    }
    builder.addCommonTServerFlag("yb_enable_read_committed_isolation", "true");
    builder.addMasterFlag("ysql_yb_ddl_transaction_block_enabled", "true");
    builder.addMasterFlag("ysql_yb_enable_ddl_savepoint_support", "true");
    builder.addMasterFlag("allowed_preview_flags_csv", "ysql_yb_enable_ddl_savepoint_support");
    // Verbose DDL verification / delete path for debugging self-abort.
    builder.addMasterFlag("vmodule", "catalog_manager=3,ysql_ddl_handler=4");
  }

  @Test
  public void matviewRewriteSelfAbort() throws Exception {
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.NONE);
    runPgRegressTest("yb_matview_rewrite_self_abort_schedule");
  }
}
