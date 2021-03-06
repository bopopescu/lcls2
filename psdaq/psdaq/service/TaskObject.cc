/*
 * Task class implementation.
 *
 *
 */

#include "TaskObject.hh"

using namespace Pds;

/*
 * Constructor: set up the default values of the task parameters
 *
 * //  _flags = THR_BOUND;  We might eventually want to use threads which
 * are each bound to a LWP (Light Weight Process). This allows greater
 * scheduling flexibility at the cost of additional complexity and privilege
 * to run them.
 *
 * UNIX specific flag values
 *  THR_SUSPENDED -- thread starts suspended, start it with thr_continue()
 *  THR_DETACHED  -- detaches thread so that resources can be reused immediately
 *  THR_BOUND     -- bind thread to a new LWP (THR_NEW_LWP not also needed)
 *  THR_NEW_LWP   -- add another LWP to pool that is running unbound threads
 *  THR_DAEMON    -- this process is not counted in the process exit decision
 */
TaskObject::TaskObject(const char* name, int priority,
                       int stackSize, char* stackBase,
                       int queueSize, unsigned flags) :
  _stacksize((size_t)stackSize),
  _stackbase(stackBase),
  _priority(priority),
  _queueSize(queueSize)
{
  _name = new char[strlen(name)+1];
  strcpy(_name, name);

  pthread_attr_init( &_flags );
  if (flags & THR_DETACHED)
  {
    pthread_attr_setdetachstate( &_flags, PTHREAD_CREATE_DETACHED );
  }

  _priority = _priority>127 ? 127 : _priority;
  _priority = _priority<0 ? 0 : _priority;
  _priority = 127 - _priority;

  size_t min_stack;

  pthread_attr_getstacksize(&_flags, &min_stack);
  if(_stacksize !=0 && _stacksize<min_stack) {
    _stacksize = min_stack;
    // also an error , probably an assert
  }
}


/*
 *
 */
TaskObject::TaskObject() :
  _name(0),
  _stacksize(0),
  _stackbase(0),
  _queueSize(0)
{
  pthread_attr_init(&_flags);
  _threadID = pthread_self();
  struct sched_param param;
  pthread_attr_getschedparam(&_flags, &param);
  _priority = param.sched_priority;
  _priority = _priority>127 ? 127 : _priority;
  _priority = _priority<0 ? 0 : _priority;
  _priority = 127 - _priority;
}


/*
 * The BIG 3 follow as TaskObject is a non-trivial object.
 */
TaskObject::TaskObject(const TaskObject& tobject )
{
  _name = new char[strlen(tobject._name)+1];
  strcpy(_name, tobject._name);
  _stacksize = tobject._stacksize;
  _stackbase = tobject._stackbase;
  _priority = tobject._priority;
  memcpy(&_flags,&tobject._flags,sizeof(pthread_attr_t));
  memcpy(&_threadID,&tobject._threadID,sizeof(pthread_t));
  _queueSize = tobject._queueSize;
}

/*
 *
 */
void TaskObject::operator= (const TaskObject& tobject)
{
  delete [] _name;
  _name = new char[strlen(tobject._name)+1];
  strcpy(_name, tobject._name);

  _stackbase = tobject._stackbase;
  _stacksize = tobject._stacksize;
  _priority = tobject._priority;
  memcpy(&_flags,&tobject._flags,sizeof(pthread_attr_t));
  memcpy(&_threadID,&tobject._threadID,sizeof(pthread_t));
  _queueSize = tobject._queueSize;
}

/*
 *
 */
TaskObject::~TaskObject()
{
  pthread_attr_destroy( &_flags );
  delete [] _name;
}
