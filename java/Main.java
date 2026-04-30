import java.util.random.RandomGenerator;

public class Main {

    public static void main(String[] args) {
        long start = System.currentTimeMillis();
        var rg = RandomGenerator.of("Xoshiro256PlusPlus");
        var bytes = new byte[1000000000];

        for (int i = 0; i < bytes.length; i++) {
            bytes[i] = (byte) rg.nextInt(200);
        }
        long end = System.currentTimeMillis();
        System.out.printf("Yes!%sms%n", end - start);
    }
}
