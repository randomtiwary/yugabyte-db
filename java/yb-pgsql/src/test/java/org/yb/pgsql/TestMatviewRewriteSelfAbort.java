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
 * Reproduces reads failing with expired/unknown transaction after
 * {@code CREATE MATERIALIZED VIEW} + {@code REFRESH} of that same view in one open
 * transaction under transactional DDL.
 *
 * <p><b>No ROLLBACK / ROLLBACK TO SAVEPOINT is required</b> for the observed failure.
 * The crash sequence issued {@code SAVEPOINT}s, then CREATE + REFRESH, then later
 * {@code SELECT}s failed while the transaction was still open.
 *
 * <p>Failure signature:
 * <pre>
 * ERROR: current transaction is expired or aborted ...
 * DETAIL: Unknown transaction, could be recently aborted: ...
 * </pre>
 *
 * <p>Why this is uncommon:
 * <ul>
 *   <li>Unusual pattern: CREATE a matview and REFRESH that same matview before the
 *       transaction ends (REFRESH rewrites DocDB: new table + drop old).</li>
 *   <li>Racey: DocDB cleanup / DDL verification timing around the rewritten tables
 *       while the client transaction is still running.</li>
 * </ul>
 *
 * <p>Flags match the savepoint randomness test environment where this was seen
 * (transactional DDL + object locks + ddl savepoint support). The savepoint
 * <em>flag</em> is part of that environment; the failure itself is not "after
 * rollback to savepoint." The SQL also includes a no-SAVEPOINT variant to help
 * separate incidental SAVEPOINT use from the CREATE+REFRESH rewrite path.
 *
 * <p>Stress:
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
    // Present in the failing environment; not claimed as the root cause by itself.
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
    builder.addMasterFlag("vmodule", "catalog_manager=3,ysql_ddl_handler=4");
  }

  @Test
  public void matviewRewriteSelfAbort() throws Exception {
    setConnMgrWarmupModeAndRestartCluster(ConnectionManagerWarmupMode.NONE);
    runPgRegressTest("yb_matview_rewrite_self_abort_schedule");
  }
}
