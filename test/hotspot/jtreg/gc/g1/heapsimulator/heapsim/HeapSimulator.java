package heapsim;

import java.time.Instant;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/**
 * A Java workload simulator to generate memory and CPU load.
 *
 * <p>Similar to fourier transforms, any heap allocation pattern can be approximated by some set of
 * fixed allocations for fixed periods of time. This simulator provides a rudimentary way of
 * entering these fixed allocations via a csv encoded in a string.
 *
 * <p>Accepted flags:
 *
 * <p>--heap_graph: alloc_size,repeated(allocation_rate,deallocation_rate,start_time): comma
 * separated
 *
 * <p>--cpu_graph: repeated(cpu_rate,start_time) pairs: comma separated
 *
 * <p>Some rules: One can have multiple heap_graphs, separated by "/". Each heap graph is a
 * consecutive tuple of 3 values: start_time (s), allocation_rate (/s), deallocation_rate (/s); and
 * is preceded by: allocation_size (B)
 *
 * <p>Of the format: [object_size](start_time,+allocation_rate,-deallocation_rate),(...)/[...](...)
 *
 * <p>e.g. '[16b](0s,+1,-1),(15s,+0,-0)/[8b](0s,+2,-1),(5s,+2,-3),(10s,+0,-0)' decomposes to: An
 * alloc rate of 1/s with a dealloc rate of 1/s for objects of 16 bytes for 15 seconds (followed by
 * idling thereafter); and an alloc rate of 2/s with a dealloc rate of 1/s for objects of 8 bytes
 * for 5 seconds followed by an alloc rate of 2/s with a dealloc rate of 3/s for objects of 8 bytes
 * for 5 seconds followed by idling thereafter.
 *
 * <p>Notice the last tuple just sets the end-time of the second-to-last tuple. Each tuple
 * essentially defines the leading edge of a square-looking graph.
 *
 * <p>There can only be one cpu graph, which consists of consecutive tuples of start_time (s),
 * cpu_rate (count) e.g. '(0s,32),(10s,64),(20s,0)' decomposes to: 32 cores for 10 seconds followed
 * by 64 cores for 10 seconds.
 *
 * <p>0,0 or blank -> no fake cpu load is added.
 *
 * <p>Notice the last tuple just sets the end-time of the second-to-last tuple.
 */
final class HeapSimulator {
  private record HeapPoint(long allocationRate, long deallocationRate, long time) {}

  private static final class HeapGraph {
    final ArrayList<HeapPoint> heapPoints;
    final long allocSize;

    HeapGraph(long allocSize) {
      this.heapPoints = new ArrayList<>();
      this.allocSize = allocSize;
    }
  }

  private record CpuPoint(long cpuRate, long time) {}

  private static class CpuLoad extends Thread {
    final List<CpuPoint> graph;
    final int threadIndex;
    final long startTime;

    CpuLoad(List<CpuPoint> graph, int threadIndex, long startTime) {
      super();
      this.graph = graph;
      this.threadIndex = threadIndex;
      this.startTime = startTime;
    }

    @Override
    public void run() {
      for (int i = 0; i < graph.size() - 1; ++i) {
        System.out.printf("ThreadIndex: %d Point: %d\n", threadIndex, i);
        long curStartTime = startTime + graph.get(i).time;
        long currentTime = System.currentTimeMillis();

        while (currentTime < curStartTime) {
          long sleepTime = curStartTime - currentTime;
          try {
            System.out.printf("ThreadIndex: %d Point: %d Sleeping %d\n", threadIndex, i, sleepTime);
            Thread.sleep(sleepTime);
          } catch (InterruptedException e) {
            e.printStackTrace();
            return;
          }
          currentTime = System.currentTimeMillis();
        }

        long nextStartTime = startTime + graph.get(i + 1).time;
        System.out.printf("ThreadIndex: %d Point: %d beginning to spin\n", threadIndex, i);
        long spins = 0;
        while (currentTime < nextStartTime) {
          if (threadIndex >= graph.get(i).cpuRate) {
            // Send to sleep.
            System.out.printf(
                "ThreadIndex: %d Point: %d nextStartTime surpassed. Sleeping\n", threadIndex, i);
            break;
          }

          // Burn cycles.
          for (int j = 0; j < 1000; ++j) {
            curStartTime = curStartTime + j;
          }
          currentTime = System.currentTimeMillis();
          ++spins;
        }
        System.out.printf("ThreadIndex: %d Point: %d Spins: %d\n", threadIndex, i, spins);
      }
    }
  }

  private static class HeapLoad extends Thread {
    final List<HeapPoint> graph;
    final long allocSize;
    final int threadIndex;
    final long startTime;

    HeapLoad(HeapGraph graph, int threadIndex, long startTime) {
      super();
      this.graph = graph.heapPoints;
      this.allocSize = graph.allocSize;
      this.threadIndex = threadIndex;
      this.startTime = startTime;
      setDaemon(true);
    }

    @Override
    public void run() {
      runInternal();
    }

    private void runInternal() {
      ArrayDeque<byte[]> heap = new ArrayDeque<>();
      Random random = new Random();
      for (int i = 0; i < graph.size() - 1; ++i) {
        System.out.printf(
            "ThreadIndex: %d Point: %d Time: %d AllocRate: %d DeallocRate: %d\n",
            threadIndex,
            i,
            graph.get(i).time,
            graph.get(i).allocationRate,
            graph.get(i).deallocationRate);
        long curStartTime = startTime + graph.get(i).time;
        long currentTime = System.currentTimeMillis();
        while (currentTime < curStartTime) {
          long sleepTime = curStartTime - currentTime;
          try {
            System.out.printf("ThreadIndex: %d Point: %d Sleeping %d\n", threadIndex, i, sleepTime);
            Thread.sleep(sleepTime);
          } catch (InterruptedException e) {
            e.printStackTrace();
            return;
          }
          currentTime = System.currentTimeMillis();
        }

        long nextStartTime = startTime + graph.get(i + 1).time;
        long allocated = 0;
        long deallocated = 0;
        double allocRate = 1.0 * graph.get(i).allocationRate / 1;
        double deallocRate = 1.0 * graph.get(i).deallocationRate / 1;
        double allocNum = allocRate;
        double deallocNum = deallocRate;
        double allocSum = graph.get(i).allocationRate * (nextStartTime - startTime) / 1000.0;
        double deallocSum = graph.get(i).deallocationRate * (nextStartTime - startTime) / 1000.0;
        System.out.printf(
            "ThreadIndex: %d Point: %d beginning to allocate %.2f items and deallocate %.2f"
                + " items\n",
            threadIndex, i, allocSum, deallocSum);
        while ((currentTime < nextStartTime)
            && ((allocated + 1 < allocSum) || (deallocated + 1 < deallocSum))) {
          // Burn heap.
          while (++allocated < allocNum) {
            heap.addLast(new byte[(int) allocSize]);
            // avoid aliasing/lazy paging magic. Stolen from autopilot Hamster.java
            random.nextBytes(heap.getLast());
          }
          while (++deallocated < deallocNum && !heap.isEmpty()) {
            heap.removeFirst();
          }

          long newCurrentTime = System.currentTimeMillis();
          if (currentTime + 1000 > newCurrentTime) {
            try {
              Thread.sleep(currentTime + 1000 - newCurrentTime);
            } catch (InterruptedException e) {
              e.printStackTrace();
              return;
            }
          }
          currentTime = System.currentTimeMillis();
          allocNum = Math.min(allocSum, allocNum + allocRate);
          deallocNum = Math.min(deallocSum, deallocNum + deallocRate);
        }
        System.out.printf(
            "ThreadIndex: %d Point: %d Size: %d Allocated: %d Deallocated: %d \n",
            threadIndex, i, allocSize, allocated, deallocated);
      }
    }
  }

  private static int parseCpuGraph(String cpuGraph, List<CpuPoint> cpuGraphList) {
    int maxCpuRate = 0;
    String cpuStr =
        cpuGraph
            .replace("[", "")
            .replace("s", "")
            .replace("b", "")
            .replace(']', ',')
            .replace("(", "")
            .replace(")", "")
            .replace("+", "")
            .replace("-", "");

    var cpuStrSplit = cpuStr.split(",");
    if (cpuStrSplit.length % 2 != 0) {
      throw new IllegalArgumentException("Invalid CPU graph length: " + cpuStrSplit.length);
    }
    for (int i = 1; i < cpuStrSplit.length; i += 2) {
      int cpuRate = Integer.parseInt(cpuStrSplit[i]);
      if (cpuRate > maxCpuRate) {
        maxCpuRate = cpuRate;
      }
      cpuGraphList.add(
          new CpuPoint(Long.parseLong(cpuStrSplit[i]), Long.parseLong(cpuStrSplit[i - 1]) * 1000));
    }
    return maxCpuRate;
  }

  private static List<HeapGraph> parseHeapGraph(String heapGraph) {
    ArrayList<HeapGraph> heapGraphs = new ArrayList<>();
    String heapFlagStr =
        heapGraph
            .replace("[", "")
            .replace(']', ',')
            .replace("s", "")
            .replace("b", "")
            .replace("(", "")
            .replace(")", "")
            .replace("+", "")
            .replace("-", "");

    var heapGraphStrs = heapFlagStr.split("/");
    if (heapGraphStrs.length == 0) {
      throw new IllegalArgumentException("Heap graph is empty!");
    }

    for (var heapGraphStr : heapGraphStrs) {
      var heapStrSplit = heapGraphStr.split(",");
      if ((heapStrSplit.length - 1) % 3 != 0 || heapStrSplit.length == 1) {
        System.err.println("heapGraphStr: " + heapGraphStr);
        throw new IllegalArgumentException("Invalid heap graph length: " + heapStrSplit.length);
      }
      heapGraphs.add(new HeapGraph(Long.parseLong(heapStrSplit[0])));
      for (int i = 3; i < heapStrSplit.length; i += 3) {
        heapGraphs
            .get(heapGraphs.size() - 1)
            .heapPoints
            .add(
                new HeapPoint(
                    Long.parseLong(heapStrSplit[i - 1]),
                    Long.parseLong(heapStrSplit[i]),
                    Long.parseLong(heapStrSplit[i - 2]) * 1000));
      }
    }
    return heapGraphs;
  }

  public static void main(String[] args) {
    List<CpuPoint> cpuGraphList = new ArrayList<>();
    int maxCpuRate = 0;
    List<HeapGraph> heapGraphs = null;

    for (String arg : args) {
      if (arg.startsWith("--heap_graph=")) {
        heapGraphs = parseHeapGraph(arg.substring("--heap_graph=".length()));
      } else if (arg.startsWith("--cpu_graph=")) {
        maxCpuRate = parseCpuGraph(arg.substring("--cpu_graph=".length()), cpuGraphList);
      } else {
        System.err.println("Unknown supported argument: " + arg);
      }
    }
    System.out.printf("maxCpuRate: %d\n", maxCpuRate);
    System.out.printf("heapGraphsSize: %d\n", heapGraphs.size());

    long startTime = Instant.now().toEpochMilli();

    // Create threads.
    ArrayList<Thread> threads = new ArrayList<>();
    for (int i = 0; i < maxCpuRate; ++i) {
      threads.add(new CpuLoad(cpuGraphList, i, startTime));
    }
    for (int i = 0; i < heapGraphs.size(); ++i) {
      threads.add(new HeapLoad(heapGraphs.get(i), i, startTime));
    }

    for (Thread t : threads) {
      t.start();
    }
    for (Thread t : threads) {
      try {
        t.join();
      } catch (InterruptedException e) {
        System.err.println(e);
      }
    }

    System.out.println("Simulation done");
  }

  private HeapSimulator() {}
}
