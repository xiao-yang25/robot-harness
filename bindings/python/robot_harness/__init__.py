"""Experimental local session client. Optional C++ extension is required by its host."""

from .session import Session, SessionError
from .navigation import NavigationSession

__all__ = ['Session', 'SessionError', 'NavigationSession']
