A ferramenta strace pode ser utilizada para ver quanto tempo o programa tomou para sair de uma systemcall, por exemplo, a seguinte sequência de comandos pode ser utilizado para verificar quanto tempo demorou para sair do recvfrom:

sudo strace -tt -T -f -o trace.txt ./bin/e7/e7_delay_test_vector_inner

-tt: timestamps absolutos
-T: mostra o tempo gasto em cada chamada
-f: segue processos filhos
-o: redireciona saída para arquivo

grep recvfrom trace.txt > only_recvfrom.txt

(filtra por linhas que contenham a chamada de sistema recvfrom)

A ferramenta perf pode ser utilizada para ver quanto tempo demorou desde o momento em que o processo ficou pronto até o momento em que ele realmente começou a rodar

sudo perf sched record -a -- <programa a ser monitorado>
sudo perf sched timehist | grep <PID do processo que queremos filtrar>

timehist motra, respectivamente, o tempo que o programa ficou esperando(bloqueado, sleep), tempo entre processo ser acordado e realmente começar a rodar, tempo que o processo ficou rodando

sudo perf sched record -a -- ./bin/e7/e7_delay_test_vector_inner &&
sudo perf sched timehist > timehist.txt

sudo perf sched record -a -- ./bin/e7/e7_delay_test_vector_inner &&
sudo perf sched timehist > timehist.txt
