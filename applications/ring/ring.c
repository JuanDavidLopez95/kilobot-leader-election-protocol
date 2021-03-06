#define SIMULATOR


#ifndef SIMULATOR
   #include <kilolib.h>
    #include <avr/io.h>  // for microcontroller register defs
    #include "ring.h"
    USERDATA myData;
    USERDATA *mydata = &myData;
#else
    #include <math.h>
    #include <kilombo.h>
    #include <stdio.h> // for printf
    #include "ring.h"
    REGISTER_USERDATA(USERDATA)
#endif

char isQueueFull()
{
    return (mydata->tail +1) % QUEUE == mydata->head;
}

/* Helper function for setting motor speed smoothly
 */
void smooth_set_motors(uint8_t ccw, uint8_t cw)
{
    // OCR2A = ccw;  OCR2B = cw;
#ifdef KILOBOT
    uint8_t l = 0, r = 0;
    if (ccw && !OCR2A) // we want left motor on, and it's off
        l = 0xff;
    if (cw && !OCR2B)  // we want right motor on, and it's off
        r = 0xff;
    if (l || r)        // at least one motor needs spin-up
    {
        set_motors(l, r);
        delay(15);
    }
#endif
    // spin-up is done, now we set the real value
    set_motors(ccw, cw);
}

void set_motion(motion_t new_motion)
{
    switch(new_motion) {
        case STOP:
            smooth_set_motors(0,0);
            break;
        case FORWARD:
            smooth_set_motors(kilo_straight_left, kilo_straight_right);
            break;
        case LEFT:
            smooth_set_motors(kilo_turn_left, 0);
            break;
        case RIGHT:
            smooth_set_motors(0, kilo_turn_right); 
            break;
    }
}


char in_interval(uint8_t distance)
{
    //if (distance >= 40 && distance <= 60)
    if (distance <= 90)
        return 1;
    return 0;
}

char is_stabilized()
{
    uint8_t i=0,j=0;
    for (i=0; i<mydata->num_neighbors; i++)
    {
        
        if ((mydata->nearest_neighbors[i].state == AUTONOMOUS && mydata->nearest_neighbors[i].num > 2) ||
            (mydata->nearest_neighbors[i].state == COOPERATIVE && mydata->nearest_neighbors[i].num_cooperative > 2))
            j++;
    }

    return j == mydata->num_neighbors;
}

// Search for id in the neighboring nodes
uint8_t exists_nearest_neighbor(uint8_t id)
{
    uint8_t i;
    for (i=0; i<mydata->num_neighbors; i++)
    {
        if (mydata->nearest_neighbors[i].id == id)
            return i;
    }
    return i;
}


// Search for id in the neighboring nodes
uint8_t are_all_cooperative()
{
    uint8_t i;
    for (i=0; i<mydata->num_neighbors; i++)
    {
        if (mydata->nearest_neighbors[i].state == COOPERATIVE)
            return 0;
    }
    return 1;
}

uint8_t get_nearest_two_neighbors()
{
    uint8_t i, l, k;
    uint16_t min_sum = 0xFFFF;
    
    k = i = mydata->num_neighbors;
    if (are_all_cooperative())
    {
        for (i=0; i<mydata->num_neighbors; i++)
        {
            // shortest
            if (mydata->nearest_neighbors[i].distance < min_sum)
            {
                k = i;
            }
        }
        if (k < mydata->num_neighbors)
        {
            i = k;
        }
    }
    else
    {
        for (i=0; i<mydata->num_neighbors; i++)
        {
            // Is it cooperative and at distance in [4cm,6cm]?
            if (mydata->nearest_neighbors[i].state == COOPERATIVE)
            {
                l = exists_nearest_neighbor(mydata->nearest_neighbors[i].right_id);
                // Does the right exits in my table?
                if (l < mydata->num_neighbors)
                {
                    if (mydata->nearest_neighbors[i].distance +
                        mydata->nearest_neighbors[l].distance < min_sum)
                    {
                        min_sum = mydata->nearest_neighbors[i].distance + mydata->nearest_neighbors[l].distance;
                        k = i;
                    }
                }
            }
        }
        if (k < mydata->num_neighbors)
        {
            i = k;
        }
    }
    return i;
}

void update_color(uint8_t *payload)
{
    if (mydata->master == 0)
    {
    	if (payload[SENDER] == mydata->my_right)
    	{
    		if (payload[COLOR] == (RGB(0,3,0)))
    		{
    			mydata->green = 3;
    		}
    		else
    		{
    			mydata->green = 0;
    		}
    	}
    }
}

void recv_sharing(uint8_t *payload, uint8_t distance)
{
    if (payload[ID] == mydata->my_id  || payload[ID] == 0 || !in_interval(distance) ) return;
    
    mydata->loneliness = 0;

    uint8_t i = exists_nearest_neighbor(payload[ID]);
    if (i >= mydata->num_neighbors) // The id has never received
    {
        if (mydata->num_neighbors < MAX_NUM_NEIGHBORS)
        {
            i = mydata->num_neighbors;
            mydata->num_neighbors++;
            mydata->nearest_neighbors[i].num = 0;
        }
    }

    mydata->nearest_neighbors[i].id = payload[ID];
    mydata->nearest_neighbors[i].right_id = payload[RIGHT_ID];
    mydata->nearest_neighbors[i].left_id = payload[LEFT_ID];
    mydata->nearest_neighbors[i].state = payload[STATE];
    mydata->nearest_neighbors[i].distance = distance;
    
    mydata->nearest_neighbors[i].message_recv_delay = 0;
    mydata->nearest_neighbors[i].is_master = payload[MASTER];
    
    if (payload[STATE] == AUTONOMOUS)
    {
        mydata->nearest_neighbors[i].num++;
        mydata->nearest_neighbors[i].num_cooperative = 0;
    }
    else
    {
        mydata->nearest_neighbors[i].num_cooperative++;
        mydata->nearest_neighbors[i].num = 0;
    }

	//printf("%d color code %d\n", payload[SENDER], RGB(0,3,0)); 
	update_color(payload);

}

void recv_joining(uint8_t *payload)
{
    //ignoring irrelevant messages
    if (payload[ID] == mydata->my_id  || payload[ID] == 0 ) return;


    //if sender set me as left, set sender as my right
    if (payload[LEFT_ID] == mydata->my_id)
    {
    	mydata->my_right = payload[SENDER];
    }
    //if sender set me as right, set sender as my left
    if (payload[RIGHT_ID] == mydata->my_id)
    {
	    mydata->my_left = payload[SENDER];
    }

    // Creates a "master" bot upon ring creation. 
    // Also boolean switch for master
    if (mydata->my_left == mydata->my_right && mydata->my_id < payload[SENDER])
    {
		mydata->red = 3;
        mydata->master = 1;
    }
#ifdef SIMULATOR
    printf("%d Left: %d Right: %d\n", mydata->my_id, mydata->my_left, mydata->my_right);
#endif
}

void recv_move(uint8_t *payload)
{
#ifdef SIMULATOR
    //printf("%d Receives move %d %d %d\n", mydata->my_id, payload[MSG], mydata->my_id, payload[RECEIVER]);
#endif
    
    if (mydata->my_id == payload[RECEIVER])
    {
        mydata->token  = 1;
        mydata->blue  = 1;
        mydata->send_token = mydata->now + TOKEN_TIME * 4.0;

    }
   /* else if (my_id == payload[SENDER])
    {
        mydata->motion_state = STOP;
    }
    else
    {
        mydata->msg.data[MSG]      = payload[MSG];
        mydata->msg.data[ID]       = mydata->my_id;
        mydata->msg.data[RECEIVER] = payload[RECEIVER];
        mydata->msg.data[SENDER]   = payload[SENDER];
        mydata->msg.type           = NORMAL;
        mydata->msg.crc            = message_crc(&msg);
        mydata->message_sent       = 0;
    } */
}

void recv_election(uint8_t *payload)
{
    /* 
        if v with w < m then
        v forwards ELECTING(w) to its clockwise neighbor and sets m := w
        v decides not to be the leader, if it has not done so already.
        else if w > m and v has not been participating then
        v sends message ELECTIN G(m) to its successor
        else if v = w then
        `v sends message ELECTED(v) to its clockwise neighbor
    */

    //May have to use the right-side
    uint8_t w = payload[MINID];
    uint8_t m = mydata->min_id;
    uint8_t v = mydata->my_id;

    if (w < v) 
    {
        mydata->readyToSendElection = 1;
        mydata->min_id = w;
    } else if (w > m && mydata->participating == 0) // && what is participating)
    {
        mydata->readyToSendElection = 1;
        mydata->participating = 1;
    }  else if (v == w)
    {
        printf("%d Leader ID\n", mydata->min_id);
        mydata->readyToSendElection = 0; //Set zero since we have found the know
        mydata->green = 255;
        mydata->red = 255;
        mydata->blue = 255;
    } 

    printf("%d Receives %d from %d and my min id is %d\n", mydata->my_id, w,payload[MINID], m);
}


void recv_elected(uint8_t *payload)
{
    /* if v receives a message ELECTED(w) with w != v then
    v forwards ELECTED(w) to its clockwise neighbor and sets leader = w
    end if */
    uint8_t w = payload[MINID];
    //uint8_t m = mydata->min_id;
    uint8_t v = mydata->my_id;

    if (w != v)  
    {
        //Forwarding part -- set readyToSendElection to true
        mydata->readyToSendElected = 1;
        mydata->readyToSendElection = 1;
    }
    
}

void message_rx(message_t *m, distance_measurement_t *d)
{
    uint8_t dist = estimate_distance(d);
    
    if (m->type == NORMAL && m->data[MSG] !=NULL_MSG)
    {
        #ifdef SIMULATOR
            //printf("%d Receives %d %d\n", mydata->my_id,  m->data[MSG], m->data[RECEIVER]);
        #endif
   
        recv_sharing(m->data, dist);
        switch (m->data[MSG])
        {
            case JOIN:
                recv_joining(m->data);
                break;
            case MOVE:
                recv_move(m->data);
                break;
            case ELECTION:
                if (m->data[ID] == mydata->my_left)
                    recv_election(m->data);
                break;
            case ELECTED:
                if (m->data[ID] == mydata->my_left)
                    recv_elected(m->data);
                break;
        }
    }
}

char enqueue_message(uint8_t m)
{
#ifdef SIMULATOR
 //   printf("%d, Prepare %d\n", mydata->my_id, m);
#endif
    if (!isQueueFull())
    {
        mydata->message[mydata->tail].data[MSG] = m;
        mydata->message[mydata->tail].data[ID] = mydata->my_id;
        mydata->message[mydata->tail].data[RIGHT_ID] = mydata->my_right;
        mydata->message[mydata->tail].data[LEFT_ID] = mydata->my_left;
        mydata->message[mydata->tail].data[RECEIVER] = mydata->my_right;
        mydata->message[mydata->tail].data[SENDER] = mydata->my_id;
        mydata->message[mydata->tail].data[STATE] = mydata->state;
        //Sending Color Data 
		mydata->message[mydata->tail].data[COLOR] = RGB(mydata->red,mydata->green,mydata->blue);
        //Sending Master Statues 

        mydata->message[mydata->tail].data[MASTER] = mydata->master;

        if (m == ELECTED || m == ELECTION)
            mydata->message[mydata->tail].data[MINID] = mydata->min_id;
    
        mydata->message[mydata->tail].type = NORMAL;
        mydata->message[mydata->tail].crc = message_crc(&mydata->message[mydata->tail]);
        mydata->tail++;
        mydata->tail = mydata->tail % QUEUE;
        return 1;
    }
    return 0;
}

/**********************************/
/**********************************/
void send_joining()
{
    uint8_t i;
    /* precondition  */
    
    if (mydata->state == AUTONOMOUS && is_stabilized()  && !isQueueFull())

    {

        i = get_nearest_two_neighbors();
        if (i < mydata->num_neighbors && mydata->message_sent == 1)
        {
            // effect:

            mydata->state = COOPERATIVE;
            mydata->my_right = mydata->nearest_neighbors[i].right_id;
            mydata->my_left = mydata->nearest_neighbors[i].id;
            enqueue_message(JOIN);
            mydata->readyToSendElection = 1;
            //set the flag to true
#ifdef SIMULATOR
            printf("Sending Joining %d right=%d left=%d\n", mydata->my_id, mydata->my_right, mydata->my_left);
#endif
        }
    }
}

void send_sharing()
{
    // Precondition
    if (mydata->now >= mydata->nextShareSending  && !isQueueFull())
    {
        // Sending
        enqueue_message(SHARE);
        // effect:
        mydata->nextShareSending = mydata->now + SHARING_TIME;
    }
}

void send_election()
{
    // Precondition
    if (mydata->readyToSendElection && !isQueueFull())
    {
        // Sending
        enqueue_message(ELECTION);
        mydata->readyToSendElection = 0;
        // effect:
    }
}

void send_elected()
{
    // Precondition
    if (mydata->readyToSendElected  && !isQueueFull())
    {
        // Sending
        enqueue_message(ELECTED);
        mydata->readyToSendElected = 0;

        // effect:
    }
}

void send_move()
{
    // Precondition:
    if (mydata->state == COOPERATIVE  && mydata->token )
    {
        mydata->send_token = mydata->now + TOKEN_TIME;
    }
    if (mydata->state == COOPERATIVE && !isQueueFull() && mydata->token && mydata->send_token <= mydata->now)
    {
            // Sending
        enqueue_message(MOVE);
        mydata->token = 0;
        mydata->blue = 0;
        // effect:
    }

}

void move(uint8_t tick)
{
    // Precondition:
    if (mydata->motion_state == ACTIVE && mydata->state == COOPERATIVE)
    {
        
      /*  if (mydata->time_active == mydata->move_motion[mydata->move_state].time)
        {
            // Effect:
            mydata->green = 1;
            mydata->move_state++;
            if (mydata->move_state == 3)
            {
                mydata->send_token = 1;
                send_move();
#ifdef SIMULATOR
                printf("Sending Move %d\n", mydata->my_id);
#endif
                mydata->motion_state = STOP;
                return;
            }
            mydata->time_active = 0;
        
        }
        set_motion(mydata->move_motion[mydata->move_state].motion);
        mydata->time_active++;
       */
    }
    else
    {
        mydata->green = 0;
        set_motion(STOP);
    }
    
}

//This functions works to reset the bots when they 
// have broken the ring 
void reset_self()
{

    //printf("%d RESET\n", mydata->my_id);
    
    mydata->state = AUTONOMOUS;
    mydata->my_left = mydata->my_right = mydata->my_id;
    mydata->num_neighbors = 0;
    
    mydata->red = 0;
    mydata->green = 0;
    mydata->blue = 0;
    
    mydata->master = 0;
}
//This function is called when message_recv_delay is > than X time
//Specific to ring
void remove_neighbor(nearest_neighbor_t lost)
{
    uint8_t lost_bot_index;
    lost_bot_index = exists_nearest_neighbor(lost.id);
    
    if (lost.id == mydata->my_right)
    {
        if (exists_nearest_neighbor(lost.right_id) < mydata->num_neighbors)
        {
            mydata->my_right = lost.right_id;
            if (lost.is_master == 1)
            {
                mydata->master = 1;
            }
        }
        else
        {
            reset_self();
            return;
        }
    }
    if (lost.id == mydata->my_left)
    {
        if (exists_nearest_neighbor(lost.left_id) < mydata->num_neighbors)
        {
            mydata->my_left = lost.left_id;
        }
        else
        {
            reset_self();
            return;
        }
    }
    mydata->nearest_neighbors[lost_bot_index] = mydata->nearest_neighbors[mydata->num_neighbors-1];    
    mydata->num_neighbors--;
}


void loop()
{
    delay(30);

    send_election();
    send_elected();
    //send_move();
    send_joining();
    send_sharing();
    move(mydata->now);

    uint8_t i;
    for (i = 0; i < mydata->num_neighbors; i++)
    {
        mydata->nearest_neighbors[i].message_recv_delay++;

        if (mydata->nearest_neighbors[i].message_recv_delay > 100)
        {
            remove_neighbor(mydata->nearest_neighbors[i]);
            break;
        }
    } 

    // Master bot color switching
    if (mydata->red == 3)
    {
        if (mydata->now % 100 == 0)
        {
            mydata->red = 0;
            mydata->green = 3;
        }
    }
    else if (mydata->red == 0 && mydata->master == 1)
    {
        if (mydata->now % 100 == 0)
        {
            mydata->red = 3;
            mydata->green = 0;
        }
    }

    set_color(RGB(mydata->red, mydata->green, mydata->blue));

    mydata->loneliness++;
    
    if (mydata->loneliness > 100)
    {
        reset_self();
    }
    mydata->now++;
}

/**
 * message_tx() is a callback for message transmission.
 * The callback is triggered every time a message is scheduled for transmission.
 * Transmission is roughly twice per second.
 * Returns a pointer to the message that should be sent. If the pointer is null, then no message is sent.
 * */
message_t *message_tx()
{
    if (mydata->tail != mydata->head)   // Queue is not empty
    {
#ifdef SIMULATOR
    //printf("%d, Sending  %d  %d\n", mydata->my_id, mydata->message[mydata->head].data[MSG] , mydata->message[mydata->head].data[RECEIVER]);
#endif
        return &mydata->message[mydata->head];
    }
    return &mydata->nullmessage;
}
 
/** Callback for successfull message transmission. 
 * It receives no parameters and returns no values.
 * Returns no values.
 * */
void message_tx_success() {
    if (mydata->tail != mydata->head) {  // Queue is not empty
#ifdef SIMULATOR
        //printf("%d Sent  %d,  %d\n", mydata->my_id, mydata->message[mydata->head].data[MSG], mydata->message[mydata->head].data[RECEIVER]);
#endif
        if (mydata->copies == 2)
        {
            mydata->head++;
            mydata->copies = 0;
            mydata->head = mydata->head % QUEUE;
        }
        else
        {
            mydata->copies++;
        }
    }
}

/**
 * The setup() function initializes various values from 
 * Returns nothing. Just initializes various values
 */
void setup() {
    rand_seed(rand_hard());
    //mydata is an instance of the USERDATA structure from the ring.h file
    mydata->my_id = rand_soft(); //Software random number generator. Assigns a random number to a bot's ID.
    
    mydata->state = AUTONOMOUS;
    mydata->my_left = mydata->my_right = mydata->my_id;
    mydata->num_neighbors = 0; //since this is a robot, 0 is false
    mydata->message_sent = 0,
    mydata->now = 0,
    mydata->nextShareSending = SHARING_TIME,
    mydata->cur_motion = STOP;
    mydata->motion_state = STOP;
    mydata->time_active = 0;
    mydata->move_state = 0;
    mydata->master = 0;  //Set Master to 0
    mydata->move_motion[0].motion = LEFT;
    mydata->move_motion[0].motion = 3;
    mydata->move_motion[1].motion = RIGHT;
    mydata->move_motion[1].motion = 5;
    mydata->move_motion[0].motion = LEFT;
    mydata->move_motion[0].motion = 2;
    mydata->red = 0,
    mydata->green = 0,
    mydata->blue = 0,
    mydata->send_token = 0;

    /* New values that were added to the USERDATA structure in the ring.h file.
        USERDATA is instantiated here as mydata. The readyToSendElection and readToSendElected
        are set initialized as false */
    mydata->readyToSendElection = 0;
    mydata->readyToSendElected = 0;
    mydata->participating = 0;
    mydata->min_id = mydata->my_id;

    mydata->nullmessage.data[MSG] = NULL_MSG;
    mydata->nullmessage.crc = message_crc(&mydata->nullmessage); //Checksum
    
    mydata->token = rand_soft() < 128  ? 1 : 0;
    //mydata->blue = mydata->token;
    mydata->head = 0;
    mydata->tail = 0;
    mydata->copies = 0;
       
#ifdef SIMULATOR
    printf("Initializing %d %d\n", mydata->my_id, mydata->token);
#endif

    mydata->message_sent = 1;
} 

#ifdef SIMULATOR
/* provide a text string for the simulator status bar about this bot */
static char botinfo_buffer[10000];
char *cb_botinfo(void)
{
    char *p = botinfo_buffer;
    p += sprintf (p, "ID: %d \n", mydata->my_id);
    //p += sprintf (p, "ID: %d \n", kilo_uid);
    if (mydata->state == COOPERATIVE)
        p += sprintf (p, "State: COOPERATIVE\n");
    if (mydata->state == AUTONOMOUS)
        p += sprintf (p, "State: AUTONOMOUS\n");
    
    return botinfo_buffer;
}
#endif

/**
 * main() function runs the kilo_init function which initializes kilobot hardware
 * This function initializes all hardware of the kilobots such as
 * registering system interrupts and the initializing the messaging subsystem.
 * kilo_start begins the kilobot event loop. The first argument supplied is a function
 * that performs any USERDATA structure variable initializations. Second argument is a function 
 * that will be called repeatedly to perform any computations required.
 * Returns nothing. 
 * */
int main() {
    /*Initializes kilobot hardware. */
    kilo_init(); 
    kilo_message_tx = message_tx;
    kilo_message_tx_success = message_tx_success;
    kilo_message_rx = message_rx;
    kilo_start(setup, loop);
    
    return 0;
}
