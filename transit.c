#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

static int queued_trains;
static int available_rails;
static int passed_trains;
static int* crossing_trains;
static pthread_mutex_t rails_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rails_cond = PTHREAD_COND_INITIALIZER;


static unsigned int train_id;
static pthread_mutex_t train_id_mutex = PTHREAD_MUTEX_INITIALIZER;

static int trains_to_create;
static int infinite_simulation;
static pthread_mutex_t train_generator_status_mutex = PTHREAD_MUTEX_INITIALIZER;

static int rails;
static int transit_time;
static int min_train_interval;
static int train_interval_variation;
static pthread_mutex_t simulation_params_mutex = PTHREAD_MUTEX_INITIALIZER;


/* Somma op1 a op2 e pone il risultato in op2 */
void add_timespec(const struct timespec* op1, struct timespec* op2){
    if(op1 == NULL || op2 == NULL){
        return;
    }

    op2->tv_nsec += op1->tv_nsec;
    if(op2->tv_nsec > 1000000000){
        op2->tv_nsec -= 1000000000;
        op2->tv_sec += 1;
    }

    op2->tv_sec += op1->tv_sec;

}

void ms_to_timespec(const int ms, struct timespec* t){
    if(t == NULL){
        return;
    }

    if(ms < 0){
        printf("Funzione (ms_to_timespec): Impossibile convertire un tempo negativo.\n");
        return;
    }

    t->tv_sec = ms / 1000;
    t->tv_nsec = (ms % 1000) * 1000000;

    return;
}

/* Sospende il processo per almeno ms millisecondi */
void ms_sleep(const int ms){
    if(ms < 0){
        return;
    }

    int tmp = 0;
    struct timespec time_left;

    ms_to_timespec(ms, &time_left);

    do {
        tmp = nanosleep(&time_left, &time_left);
    } while (tmp != 0 && errno == EINTR);

    return; 
}

int generate_random_train_time_arrival(const int min_time, const int variable_interval){
    int min = min_time;
    
    if(min_time < 0){
        min = 0;
    }
    if(variable_interval < 0){
        return min;
    }
    return (rand() % variable_interval) + min;

}

void log_trains(const int n_waiting, const int n_in_transit, const int n_passed, const int* in_transit_ids, const int n_rails){

    time_t lt = time(NULL);
    struct tm time_struct = *localtime(&lt);

    printf("\n(%02d:%02d:%02d) Cambiamento:\n\tTreni in attesa:    %d\n\tTreni in transito:  %d\n\t\tdi cui id -> [",
    time_struct.tm_hour, time_struct.tm_min, time_struct.tm_sec, n_waiting, n_in_transit);

    if(in_transit_ids != NULL && n_rails > 0){
        for(int i=0; i < n_rails; ++i){
            if(i)
                printf(" ");
            if(in_transit_ids[i] == -1){
                printf("-");
            }
            else{
                printf("%d", in_transit_ids[i]);
            }
        }
    }
    printf("]\n\tTreni gia' passati: %d\n", n_passed);
}


/* La funzione che implementa le funzionalita' di un treno, da eseguire da un thread per treno */
void* train_simulator(){
    pthread_t my_tid;
    int my_id;
    struct timespec cond_wait_timeout_time;
    struct timespec cond_wait_absolute_time;

    my_tid = pthread_self();

    /* Si ottiene l'id unico */

    pthread_mutex_lock(&train_id_mutex);

    my_id = train_id;
    ++train_id;
    
    pthread_mutex_unlock(&train_id_mutex);

    /*  
        Fase 1: il treno attende di poter occupare un binario, altrimenti si pone in attesa.
        Se un binario è libero e il logger ha scritto l'ultimo cambiamento, il treno occupa un binario.
    */

    /* Non è necessaria la sincronizzazione con un mutex perchè so che transit_time non sarà modificato mentre ci sono thread-treni in esecuzione */
    ms_to_timespec(transit_time * 2, &cond_wait_timeout_time);

    int my_rail = -1;
    int queue = 0;
    int wait_ret_value;

    pthread_mutex_lock(&rails_mutex);

    while(my_rail == -1){
        if(available_rails == 0){
            if(queue == 0){
                queue = 1;
                ++queued_trains;

                log_trains(queued_trains, rails - available_rails, passed_trains, crossing_trains, rails);
            }
            
            clock_gettime(CLOCK_REALTIME, &cond_wait_absolute_time);
            add_timespec(&cond_wait_timeout_time, &cond_wait_absolute_time);

            wait_ret_value = pthread_cond_timedwait(&rails_cond, &rails_mutex, &cond_wait_absolute_time);

            if(wait_ret_value == 0 && errno == ETIMEDOUT){
                printf("Treno %d: timeout sull'attesa per l'accesso ai binari.\n", my_id);
            }
        }
        else{
            if(queue == 1){
                --queued_trains;
            }

            --available_rails;

            int transit_rail_occupied = 0;
            for(int i=0; i < rails && !transit_rail_occupied; ++i){
                if(crossing_trains[i] == -1){
                    crossing_trains[i] = my_id;
                    transit_rail_occupied = 1;
                    my_rail = i;
                }
            }

            if(transit_rail_occupied == 0){
                printf("Treno %d: inconsistenza tra binari disponibili e array dei treni in transito, termino.\n", my_id);
                ++available_rails;
                pthread_mutex_unlock(&rails_mutex);
                return NULL;
            }
            else{
                /* Ha occupato con successo il binario e può transitare */
                log_trains(queued_trains, rails - available_rails, passed_trains, crossing_trains, rails);

                pthread_mutex_unlock(&rails_mutex);
            }
        }
    }

    /*
        Fase 2: il treno è in transito su un binario
    */

    ms_sleep(transit_time);

    /*
        Fase 3: il treno ha finito di transitare e deve rendere libero il binario.
    */

    pthread_mutex_lock(&rails_mutex);
            
    ++available_rails;
    crossing_trains[my_rail] = -1;
    ++passed_trains;

    log_trains(queued_trains, rails - available_rails, passed_trains, crossing_trains, rails);

    pthread_cond_broadcast(&rails_cond);
    pthread_mutex_unlock(&rails_mutex);

    //printf("Treno %d: ho attraversato la stazione con successo.\n", my_id);
    return NULL;
}


/* Gestisce la creazione dei treni nell'intervallo min_time_interval - max_time_interval */
void* train_generator(){

    /* Non serve una sincronizzazione per l'accesso a questi parametri */
    int min_interval = min_train_interval;
    int interval_variation = train_interval_variation;
    

    pthread_mutex_lock(&train_generator_status_mutex);

    pthread_attr_t train_thread_attr;
    pthread_t new_train_thread;

    pthread_attr_init(&train_thread_attr);
    pthread_attr_setdetachstate(&train_thread_attr, PTHREAD_CREATE_DETACHED);
    
    while(trains_to_create > 0){
        /* Genera un nuovo thread treno */

        if(pthread_create(&new_train_thread, &train_thread_attr, train_simulator, NULL) == -1){
            printf("Thread generatore: impossibile creare un nuovo thread - treno, termino.\n");
            pthread_attr_destroy(&train_thread_attr);
            pthread_mutex_unlock(&train_generator_status_mutex);
            return NULL;
        }
        else{
            --trains_to_create;
            ms_sleep(generate_random_train_time_arrival(min_interval, interval_variation));
        }
    }

    pthread_attr_destroy(&train_thread_attr);
    pthread_mutex_unlock(&train_generator_status_mutex);

    printf("\nThread generatore: termino.\n");
    return NULL;
}


void print_help(){
    printf("\nParametri (in ordine):\n\t- N binari (intero > 0),\n\t- T tempo in ms di transito (intero > 0),\n\t- Tmin tempo in ms minimo prima di un nuovo treno (intero > 0),\n\t- Tmax tempo in ms massimo prima di un nuovo treno (intero tale che Tmax > Tmin),\n\t- [FACOLTATIVO] N_TRENI che verranno generati (intero > 0) se non specificato è 100.\n");
}


/*
    Parametri:  N binari, T tempo per transitare, Tmin tempo minimo prima dell'arrivo di un nuovo treno, 
                Tmax tempo massimo prima dell'arrivo di un nuovo treno
    I tempi sono in millisecondi
*/
int main(int argc, char* argv[]){

    if(argc < 2){
        printf("Errore: specifica i parametri del programma.\n");
        print_help();
        return -1;
    }

    if(argc < 5){
        printf("Errore: specifica tutti i paramentri necessari.\n");
        print_help();
        return -1;
    }

    srand(time(NULL));

    int max_train_interval;
    int trains_to_simulate;

    rails = atoi(argv[1]);
    transit_time = atoi(argv[2]);
    min_train_interval = atoi(argv[3]);
    max_train_interval = atoi(argv[4]);


    if(argc >= 6){
        trains_to_simulate = atoi(argv[5]);
    }
    else{
        trains_to_simulate = 100;
        printf("Numero di treni non specificato, default = %d.\n", trains_to_simulate);
    }

    if(rails <= 0){
        printf("Errore: non può esserci meno di 1 binario (N > 0).\n");
        return -1;
    }
    if(transit_time < 0){
        printf("Errore: il tempo di transito non può essere negativo (T > 0).\n");
        return -1;
    }
    if(min_train_interval < 1){
        printf("Errore: l'intervallo minimo fra treni non può essere minore di 1 (Tmin > 0).\n");
        return -1;
    }
    if(max_train_interval <= min_train_interval){
        printf("Errore: l'intervallo massimo fra treni deve essere maggiore di quello minimo (Tmax > Tmin).\n");
        return -1;
    }
    if(trains_to_simulate <= 0){
        printf("Errore: numero di treni da simulare non corretto (numTrains > 0)\n");
        return -1;
    }

    train_interval_variation = max_train_interval - min_train_interval;

    printf("Info: parsing dei parametri completato.\n\n");

    ms_sleep(1000); /* Solo per leggibilità durante l'esecuzione */

    /* Fine del parsing dei parametri */

    int train_generator_return_value;
    pthread_t train_generator_tid;
    pthread_attr_t train_generator_attr;

    /* Preparazione all'avvio della simulazione */
    queued_trains = 0;
    available_rails = rails;
    passed_trains = 0;
    
    crossing_trains = (int*) malloc(rails * sizeof(int));
    if(crossing_trains == NULL){
        printf("Errore: impossibile allocare l'array di treni in attraversamento.\n");
        return -1;
    }
    for(int i = 0; i < rails; ++i){
        /* Un binario posto a -1 indica che non è occupato */
        crossing_trains[i] = -1;
    }

    train_id = 1;

    /* Avvio il thread generatore di treni */
    pthread_attr_init(&train_generator_attr);
    pthread_attr_setdetachstate(&train_generator_attr, PTHREAD_CREATE_JOINABLE);

    trains_to_create = trains_to_simulate;
    infinite_simulation = 0;

    if(pthread_create(&train_generator_tid, NULL, train_generator, NULL) != 0){
        printf("Thread Principale: impossibile creare il thread generatore di treni.\n");
        return -1;
    }
    else{
        printf("Thread Principale: Thread generatore di treni creato.\n");
    }

    /* Attendo che il thread generatore finisca */
    pthread_join(train_generator_tid, NULL);
    pthread_attr_destroy(&train_generator_attr);


    /* Attendo che tutti i processi treno siano transitati */
    
    if(trains_to_create > 0){
        printf("Thread principale: non sono stati creati tutti i treni.\n");
    }

    
    int fine_simulazione = 0;

    while(!fine_simulazione){
        ms_sleep(transit_time);
        
        pthread_mutex_lock(&rails_mutex);
        if(available_rails == rails && queued_trains == 0){
            fine_simulazione = 1;
        }
        pthread_mutex_unlock(&rails_mutex);
    }

    free(crossing_trains);

    printf("\n\n\nThread principale: fine simulazione!\n");
    return 0;
}
