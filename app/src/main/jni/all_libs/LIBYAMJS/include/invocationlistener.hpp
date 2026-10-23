#ifndef __YAMPP_INVOCATION_LISTENER_HPP__
#define __YAMPP_INVOCATION_LISTENER_HPP__

#include "gumpp.hpp"

typedef struct _YamInvocationListener YamInvocationListener;

namespace Yam
{
  typedef struct _YamInvocationListenerProxy YamInvocationListenerProxy;

  class InvocationListenerProxy : public Object
  {
  public:
    InvocationListenerProxy (InvocationListener * listener);
    virtual ~InvocationListenerProxy ();

    virtual void ref ();
    virtual void unref ();
    virtual void * get_handle () const;

    virtual void on_enter (InvocationContext * context);
    virtual void on_leave (InvocationContext * context);

  protected:
    YamInvocationListenerProxy * cproxy;
    InvocationListener * listener;
  };

  class ProbeListenerProxy : public Object
  {
  public:
    ProbeListenerProxy (ProbeListener * listener);
    virtual ~ProbeListenerProxy ();

    virtual void ref ();
    virtual void unref ();
    virtual void * get_handle () const;

    virtual void on_hit (InvocationContext * context);

  protected:
    YamInvocationListener * cproxy;
    ProbeListener * listener;
  };
}

#endif
