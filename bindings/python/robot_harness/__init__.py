"""Experimental local session client. Optional C++ extension is required by its host."""

from .session import Session, SessionError

__all__ = ['Session', 'SessionError']
