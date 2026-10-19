public class ComprehensiveTest {
    public static synchronized void syncMethod(int depth) {
        if (depth > 0) {
            syncMethod(depth - 1);
        }
    }

    public static void testExceptions() {
        try {
            throw new IllegalArgumentException("Test exception propagation");
        } catch (IllegalArgumentException e) {
            System.out.println("Caught expected exception: " + e.getMessage());
        }
    }

    public static long fib(int n) {
        if (n <= 1) return n;
        return fib(n - 1) + fib(n - 2);
    }

    public static void main(String[] args) {
        System.out.println("[Test 1] Testing basic arithmetic & recursion...");
        long res = fib(10);
        System.out.println("fib(10) = " + res);
        if (res != 55) {
            System.err.println("Assertion failed: fib(10) != 55");
            System.exit(1);
        }

        System.out.println("[Test 2] Testing recursive synchronized methods...");
        syncMethod(5);
        System.out.println("Synchronized methods OK!");

        System.out.println("[Test 3] Testing exception throw & catch...");
        testExceptions();

        System.out.println("[Test 4] Testing collections & hashing...");
        java.util.Map<String, Integer> map = new java.util.HashMap<String, Integer>();
        for (int i = 0; i < 20; i++) {
            map.put("key_" + i, i * 2);
        }
        System.out.println("Map size: " + map.size() + ", key_10: " + map.get("key_10"));

        System.out.println("ALL TESTS PASSED SUCCESSFULLY ON RISC-V OPENJDK 8U!");
    }
}
