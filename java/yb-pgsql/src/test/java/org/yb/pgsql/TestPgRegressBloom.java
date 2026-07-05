package org.yb.pgsql;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.util.BuildTypeUtil;
import org.yb.YBTestRunner;
@RunWith(value=YBTestRunner.class)
public class TestPgRegressBloom extends BasePgRegressTest {
  @Override public int getTestMethodTimeoutSec() {
    return BuildTypeUtil.nonSanitizerVsSanitizer(2100, 2700);
  }
  @Test public void testPgRegressBloom() throws Exception {
    runPgRegressTest("yb_bloom_schedule");
  }
}
