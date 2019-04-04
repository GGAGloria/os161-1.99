#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#define MAX_THREADS 10

//define the struct Vehicle with origin and destination
typedef struct Vehicle
{
  Direction origin;
  Direction destination;
} Vehicle;

static Vehicle * volatile vehicles[MAX_THREADS]; //the queue
static struct cv *cv;
static struct lock *lk;
static int NumThreads = 10;      // number of concurrent simulation threads
volatile int NumThreadsInQueue = 0; //


static bool right_turn(Vehicle *v);
static bool check_constraints(Vehicle *v);

//check whether the vehicle is making a right turn
bool
right_turn(Vehicle *v) {
  KASSERT(v != NULL);
  if (((v->origin == west) && (v->destination == south)) ||
      ((v->origin == south) && (v->destination == east)) ||
      ((v->origin == east) && (v->destination == north)) ||
      ((v->origin == north) && (v->destination == west))) {
    return true;
  } else {
    return false;
  }
}

//check whether the new vehicle has a confliction with the vehicles in queue now
bool
check_constraints(Vehicle *v) {
  int i;
  /* compare newly-added vehicle to each other vehicles in in the intersection */
  for(i=0;i<NumThreads;i++) {
    if (vehicles[i]==NULL) continue;
    /* no conflict if both vehicles have the same origin */
    if (v->origin == vehicles[i]->origin) continue;
    /* no conflict if vehicles go in opposite directions */
    if ((vehicles[i]->origin == v->destination) &&
        (vehicles[i]->destination == v->origin)) continue;
    /* no conflict if one makes a right turn and 
       the other has a different destination */
    if ((right_turn(vehicles[i]) || right_turn(v)) &&
  (v->destination != vehicles[i]->destination)) continue;
    
    return false;
  }
  return true;
}


/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */
  for (int i=0; i<MAX_THREADS; i++){
    vehicles[i] = (Vehicle * volatile) NULL;
  }
  cv = cv_create("CV");
  lk = lock_create("Lock");
  if (cv == NULL) {
    panic("could not create cv");
  }
  if (lk == NULL) {
    panic("could not create lock");
  }
  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(cv != NULL);
  cv_destroy(cv);
  KASSERT(lk != NULL);
  lock_destroy(lk);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{
  KASSERT(cv!=NULL);
  KASSERT(lk!=NULL);
  Vehicle *v = kmalloc(sizeof(struct Vehicle));
  v->origin = origin;
  v->destination = destination;
  lock_acquire(lk);
  while (check_constraints(v) == false || NumThreads == NumThreadsInQueue){
    cv_wait(cv,lk);
  }
  NumThreadsInQueue++;
  int i;
  for (i=0; i<NumThreads; i++){
    if (vehicles[i]==NULL) {
      vehicles[i] = v;
      break;
    }
  }
  
  lock_release(lk);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  /* replace this default implementation with your own implementation */
  KASSERT(cv!=NULL);
  KASSERT(lk!=NULL);
  lock_acquire(lk);
  for (int i=0; i<NumThreads; i++){
    if (vehicles[i]!=NULL){
      if (vehicles[i]->origin == origin && vehicles[i]->destination == destination){
        vehicles[i] = NULL;
        NumThreadsInQueue --;
        cv_broadcast(cv,lk);
        break;
      }
    }  
  }
  lock_release(lk);
}
