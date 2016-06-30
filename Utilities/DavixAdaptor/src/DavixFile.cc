#include "Utilities/DavixAdaptor/interface/DavixFile.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/EDMException.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include <cassert>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <davix.hpp>

using namespace Davix;

static Context* davix_context_s = NULL;


DavixFile::DavixFile (void)
{}


DavixFile::DavixFile (const char *name,
    	    int flags /* = IOFlags::OpenRead */,
    	    int perms /* = 066 */)
{open (name, flags, perms);}


DavixFile::DavixFile (const std::string &name,
    	    int flags /* = IOFlags::OpenRead */,
    	    int perms /* = 066 */)
{open (name.c_str (), flags, perms);}


DavixFile::~DavixFile (void)
{
    close();
    return;
}


Context* DavixFile::getDavixInstance()
{
  if (davix_context_s == NULL) {
      davix_context_s = new Context();
      }
  return davix_context_s;
}


void
DavixFile::close (void)
{
  return;
} 


void
DavixFile::abort (void)
{
  return;
}


void
DavixFile::create (const char *name,
		    bool exclusive /* = false */,
		    int perms /* = 066 */)
{
  open (name,
        (IOFlags::OpenCreate | IOFlags::OpenWrite | IOFlags::OpenTruncate
         | (exclusive ? IOFlags::OpenExclusive : 0)),
        perms);
}


void
DavixFile::create (const std::string &name,
                    bool exclusive /* = false */,
                    int perms /* = 066 */)
{
  open (name.c_str (),
        (IOFlags::OpenCreate | IOFlags::OpenWrite | IOFlags::OpenTruncate
         | (exclusive ? IOFlags::OpenExclusive : 0)),
         perms);
}


void
DavixFile::open (const std::string &name,
                  int flags /* = IOFlags::OpenRead */,
                  int perms /* = 066 */)
{ open (name.c_str (), flags, perms); }


void
DavixFile::open (const char *name,
                  int flags /* = IOFlags::OpenRead */,
                  int perms /* = 066 */)
{
  // Actual open
  if ((name == 0) || (*name == 0)) {
    edm::Exception ex(edm::errors::FileOpenError);
    ex << "Cannot open a file without name";
    ex.addContext("Calling DavixFile::open()");
    throw ex;
  }

  if ((flags & (IOFlags::OpenRead | IOFlags::OpenWrite)) == 0) {
    edm::Exception ex(edm::errors::FileOpenError);
    ex << "Must open file '" << name << "' at least for read or write";
    ex.addContext("Calling DavixFile::open()");
    throw ex;
  }

  // Translate our flags to system flags
  int openflags = 0;

  if ((flags & IOFlags::OpenRead) && (flags & IOFlags::OpenWrite))
    openflags |= O_RDWR;
  else if (flags & IOFlags::OpenRead)
    openflags |= O_RDONLY;
  else if (flags & IOFlags::OpenWrite)
    openflags |= O_WRONLY;

  DavixError* davixErr = NULL;

  davixPosix = new DavPosix(getDavixInstance());
  m_fd = davixPosix->open(NULL, name, O_RDONLY, &davixErr);

  // Check Davix Error
  if (davixErr || m_fd == NULL)
  {
    edm::Exception ex(edm::errors::FileReadError);
    ex << "Davix open (name='" << m_name << ") failed with "
       << "error '" << davixErr->getErrMsg().c_str()
       << " and error code " << davixErr->getStatus();
    ex.addContext("Calling DavixFile::open()");
    throw ex;
  }
  m_name = name;
}

IOSize
DavixFile::readv (IOBuffer *into, IOSize buffers)
{
  assert (! buffers || into);

  // Davix does not support 0 buffers;
  if (! buffers)
    return 0;

  DavixError* davixErr = NULL;

  DavIOVecInput input_vector[buffers];
  DavIOVecOuput output_vector[buffers];
  IOSize total = 0; // Total requested bytes
  for (IOSize i = 0; i < buffers; ++i)
  {
    input_vector[i].diov_size = into [i].size ();
    input_vector[i].diov_buffer =  (char *) into [i].data();
    total += into [i].size ();
  }

  ssize_t s = davixPosix->preadVec (m_fd, input_vector, output_vector, buffers, &davixErr);
  if (davixErr || s < 0)
  {
    edm::Exception ex(edm::errors::FileReadError);
    ex << "Davix readv (name='" << m_name << "', buffers=" << (buffers)
       << ") failed with error '" << davixErr->getErrMsg().c_str()
       << " and error code " << davixErr->getStatus()
       << " and call returned " << s << " bytes";
    ex.addContext("Calling DavixFile::read()");
    throw ex;
  }
  return total;
}

IOSize
DavixFile::read (void *into, IOSize n)
{
  DavixError* davixErr = NULL;
  davixPosix->fadvise(m_fd, 0, n, AdviseRandom);
  IOSize done = 0;
  while (done < n)
  {
    ssize_t s = davixPosix->read (m_fd, (char *) into + done, n - done, &davixErr);
    if (davixErr || s < 0)
    {
      edm::Exception ex(edm::errors::FileReadError);
      ex << "Davix read (name='" << m_name << "', n=" << (n-done)
         << ") failed with error '" << davixErr->getErrMsg().c_str()
         << " and error code " << davixErr->getStatus()
         << " and call returned " << s << " bytes";
      ex.addContext("Calling DavixFile::read()");
      throw ex;
    }
    else if (s == 0)
      // end of file
      break;
    done += s;
  }
  return done;
}


IOSize
DavixFile::write (const void *from, IOSize n)
{
  IOSize done = 0;
  DavixError* davixErr = NULL;
  while (done < n)
  {
    ssize_t s = davixPosix->pwrite (m_fd, (const char *) from + done, n, n - done, &davixErr);
    if (davixErr || s < -1) {
      cms::Exception ex("FileWriteError");
      ex << "Davix write(name='" << m_name << "', n=" << (n-done)
         << ") failed with error '" << davixErr->getErrMsg().c_str()
         << " and error code " << davixErr->getStatus()
         << " and call returned " << s << " bytes";
      ex.addContext("Calling DavixFile::write()");
      throw ex;
    }
    done += s;
  }
  return done;
}


IOOffset
DavixFile::position (IOOffset offset, Relative whence /* = SET */)
{
  DavixError* davixErr = NULL;
  if (whence != CURRENT && whence != SET && whence != END) {
    cms::Exception ex("FilePositionError");
    ex << "DavixFile::position() called with incorrect 'whence' parameter";
    throw ex;
  }
  IOOffset result;
  size_t mywhence = (whence == SET ? SEEK_SET
		    	    : whence == CURRENT ? SEEK_CUR
                    : SEEK_END);

  if ((result = davixPosix->lseek (m_fd, offset, mywhence, &davixErr)) == -1) {
    cms::Exception ex("FilePositionError");
    ex << "dc_lseek64(name='" << m_name << "', offset=" << offset
       << ", whence=" << mywhence << ") failed with "
       << "error " << davixErr->getErrMsg().c_str() << " and "
       << "error code " << davixErr->getStatus() << " and "
       << "call returned " << result;
    ex.addContext("Calling DavixFile::position()");
    throw ex;
  }

  return result;
}


void
DavixFile::resize (IOOffset /* size */)
{
  cms::Exception ex("FileResizeError");
  ex << "DavixFile::resize(name='" << m_name << "') not implemented";
  throw ex;
}
        
