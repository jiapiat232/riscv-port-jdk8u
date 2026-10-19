import java.lang.ref.WeakReference;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

public class ComprehensiveTest {
    private static final Object SHARED_LOCK = new Object();
    private static volatile boolean childReady = false;
    private static volatile boolean parentNotified = false;
    private static int sharedCounter = 0;

    public static synchronized void syncStaticMethod(int depth) {
        if (depth > 0) {
            syncStaticMethod(depth - 1);
        }
    }

    public synchronized int syncInstanceMethod(int depth) {
        if (depth <= 0) {
            return 42;
        }
        synchronized (this) {
            return syncInstanceMethod(depth - 1) + 1;
        }
    }

    public static void testExceptions() {
        int finallyCount = 0;
        Object lock = new Object();
        try {
            synchronized (lock) {
                throw new IllegalArgumentException("Test exception propagation under lock");
            }
        } catch (IllegalArgumentException e) {
            System.out.println("Caught expected exception: " + e.getMessage());
        } finally {
            finallyCount++;
        }
        // Verify lock was properly released during exception stack unrolling
        synchronized (lock) {
            finallyCount += 10;
        }
        if (finallyCount != 11) {
            throw new RuntimeException("finallyCount mismatch: " + finallyCount);
        }
    }

    public static void testThreadsAndBiasRevocation() throws Exception {
        Thread worker = new Thread(new Runnable() {
            @Override
            public void run() {
                for (int i = 0; i < 100; i++) {
                    synchronized (SHARED_LOCK) {
                        sharedCounter++;
                    }
                }
                synchronized (SHARED_LOCK) {
                    childReady = true;
                    SHARED_LOCK.notifyAll();
                    while (!parentNotified) {
                        try {
                            SHARED_LOCK.wait(1000);
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                        }
                    }
                }
            }
        }, "RISCV-Test-Worker");

        // First acquire SHARED_LOCK on main thread (biases toward main thread when BiasedLockingStartupDelay=0)
        synchronized (SHARED_LOCK) {
            sharedCounter += 10;
        }

        // Start worker thread which contends on SHARED_LOCK -> triggers bias revocation safepoint!
        worker.start();

        synchronized (SHARED_LOCK) {
            while (!childReady) {
                SHARED_LOCK.wait(1000);
            }
            parentNotified = true;
            SHARED_LOCK.notifyAll();
        }
        worker.join();

        if (sharedCounter != 110) {
            throw new RuntimeException("Expected sharedCounter == 110, got " + sharedCounter);
        }
        System.out.println("Multi-thread bias revocation & wait/notify OK! sharedCounter = " + sharedCounter);
    }

    public static void testGCSafepointAndBarriers() {
        Object referent = new Object();
        WeakReference<Object> weakRef = new WeakReference<Object>(referent);
        if (weakRef.get() == null) {
            throw new RuntimeException("WeakReference cleared prematurely");
        }

        // Test reference arraycopy barriers (gen_write_ref_array_pre/post_barrier)
        String[] src = new String[] { "riscv", "jdk8u", "hotspot", "interpreter", "gc_barrier" };
        String[] dst = new String[5];
        System.arraycopy(src, 0, dst, 0, src.length);
        if (!Arrays.equals(src, dst)) {
            throw new RuntimeException("Reference System.arraycopy failed");
        }

        // Clear strong reference and trigger full GC safepoint
        referent = null;
        System.gc();

        System.out.println("System.gc() safepoint & WeakReference OK! weakRef.get() = " + weakRef.get());
    }

    public static long fib(int n) {
        if (n <= 1) return n;
        return fib(n - 1) + fib(n - 2);
    }

    public static void main(String[] args) throws Exception {
        System.out.println("[Test 1] Testing basic arithmetic & recursion...");
        long res = fib(10);
        System.out.println("fib(10) = " + res);
        if (res != 55) {
            throw new RuntimeException("Assertion failed: fib(10) != 55");
        }

        System.out.println("[Test 2] Testing recursive synchronized static/instance methods & blocks...");
        syncStaticMethod(5);
        ComprehensiveTest instance = new ComprehensiveTest();
        int instRes = instance.syncInstanceMethod(5);
        if (instRes != 47) {
            throw new RuntimeException("Assertion failed: instRes != 47, got " + instRes);
        }
        System.out.println("Synchronized methods & recursive blocks OK! instRes = " + instRes);

        System.out.println("[Test 3] Testing exception throw, catch & monitor unrolling...");
        testExceptions();

        System.out.println("[Test 4] Testing collections & hashing...");
        Map<String, Integer> map = new HashMap<String, Integer>();
        for (int i = 0; i < 20; i++) {
            map.put("key_" + i, i * 2);
        }
        System.out.println("Map size: " + map.size() + ", key_10: " + map.get("key_10"));

        System.out.println("[Test 5] Testing multi-threaded synchronization, bias revocation & wait/notify...");
        testThreadsAndBiasRevocation();

        System.out.println("[Test 6] Testing System.gc() safepoint, WeakReference & oop_arraycopy barriers...");
        testGCSafepointAndBarriers();

        System.out.println("ALL TESTS PASSED SUCCESSFULLY ON RISC-V OPENJDK 8U!");
    }
}
