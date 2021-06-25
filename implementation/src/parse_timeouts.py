

f = open('timeouts13', 'r')
lines = f.readlines()

max_good = 0
max_bad = 0
avg_good = 0
avg_bad = 0
bad_count = 0
good_count = 0

for line in lines:
    timeout = line[72]
    metric_str = line[74:-1]
    timeout_flag = int(timeout)
    metric = float(metric_str)
    print(timeout, metric)
    if (timeout_flag == 1):
        if (metric > max_bad):
            max_bad = metric
        bad_count += 1
        avg_bad += metric
    else:
        if (metric > max_good):
            max_good = metric
        good_count += 1
        avg_good += metric
print("good count", good_count)
print("bad count", bad_count)

print("max good", max_good)
print("max bad", max_bad)

print("avg good", avg_good / good_count)
print("avg bad", avg_bad / bad_count)
