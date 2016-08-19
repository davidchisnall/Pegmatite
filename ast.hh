/*-
 * Copyright (c) 2012, Achilleas Margaritis
 * Copyright (c) 2014-2016, David T. Chisnall
 * Copyright (c) 2016, Jonathan Anderson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef PEGMATITE_AST_HPP
#define PEGMATITE_AST_HPP


#include <cassert>
#include <list>
#include <unordered_map>
#include <sstream>
#include <memory>
#include <cxxabi.h>
#include "parser.hh"


namespace pegmatite {

/**
 * Demangle an ABI-specific C++ type name, if possible.
 * @returns   the demangled name or, if not possible, the original name
 */
std::string demangle(std::string);

#ifdef DEBUG_AST_CONSTRUCTION
template <class T> void debug_log(const char *msg, size_t depth, T *obj)
{
	std::string demangled = demangle(typeid(*obj).name());
	fprintf(stderr, "[%zd] %s %s (%p) off the AST stack\n",
			depth, msg, demangled.c_str(), static_cast<const void*>(obj));
}
#else
template <class T> void debug_log(const char *, size_t /* depth */, T *) {}
#endif // DEBUG_AST_CONSTRUCTION

class ASTNode;
template <class T, bool Optional> class ASTPtr;
template <class T> class ASTList;
template <class T> class BindAST;


typedef std::pair<const InputRange, std::unique_ptr<ASTNode>> ASTStackEntry;
/** type of AST node stack.
 */
typedef std::vector<ASTStackEntry> ASTStack;

#ifdef USE_RTTI
#define PEGMATITE_RTTI(thisclass, superclass)
#else
/**
 * Define the methods required for pegmatite's lightweight RTTI replacement to
 * work.  This should be used at the end of the class definition and will
 * provide support for safe downcasting.
 */
#define PEGMATITE_RTTI(thisclass, superclass)            \
	friend ASTNode;                                      \
protected:                                               \
	static char *classKind()                             \
	{                                                    \
		static char thisclass ## id;                     \
		return &thisclass ## id;                         \
	}                                                    \
public:                                                  \
	virtual bool isa(char *x) override                   \
	{                                                    \
		return (x == classKind()) ||                     \
				(superclass::isa(x));                    \
	}
#endif


/**
 * Base class for AST nodes.
 */
class ASTNode
{
public:
	/**
	 * Default constructor.
	 */
	ASTNode() {}

	/**
	 * Copying AST nodes is not supported.
	 */
	ASTNode(const ASTNode&) = delete;

	/**
	 * Interface for constructing the AST node.  The input range `r` is the
	 * range within the source.
	 */
	virtual bool construct(const InputRange &r, ASTStack &st,
	                       const ErrorReporter&) = 0;

	/**
	 * Destructor does nothing, virtual for subclasses to use.
	 * Defined out-of-line to avoid emitting vtables in every translation
	 * unit that includes this header.
	 */
	virtual ~ASTNode();

private:

	template <class T, bool Optional> friend class ASTPtr;
	template <class T> friend class ASTList;
	template <class T> friend class BindAST;

#ifndef USE_RTTI
protected:
	/**
	 * Returns the kind of object class.  This is a unique pointer that can be
	 * tested against pointers returned by classKind() to determine whether
	 * they can be safely compared.
	 */
	virtual char *kind() { return classKind(); }
	/**
	 * Returns the unique identifier for this class.
	 */
	static char *classKind()
	{
		static char ASTNodeid;
		return &ASTNodeid;
	}
public:
	/**
	 * Root implementation of the RTTI-replacement for builds not wishing to
	 * use RTTI.  This returns true if `x` is the value returned from
	 * `classKind()`, or false otherwise.
	 */
	virtual bool isa(char *x)
	{
		return x == classKind();
	}
	/**
	 * Returns true if this object is an instance of `T`.  Note that this
	 * *only* works with single-inheritance hierarchies.  If you wish to use
	 * multiple inheritance in your AST classes, then you must define
	 * `USE_RTTI` and use the C++ RTTI mechanism.
	 */
	template <class T> bool isa()
	{
		return isa(T::classKind());
	}
	/**
	 * Returns a pointer to this object as a pointer to a child class, or
	 * `nullptr` if the cast would be unsafe.
	 *
	 * Note that AST nodes are intended to be always used as unique pointers
	 * and so the returned object is *only* valid as long as the unique pointer
	 * is valid.
	 */
	template <class T> T* get_as()
	{
		auto *t = this;
		return t ? (isa<T>() ? static_cast<T*>(this) : nullptr) : nullptr;
	}
#else
public:
	template <class T> T* get_as()
	{
		return dynamic_cast<T*>(this);
	}
#endif
};


class ASTMember;


/** type of ast member vector.
 */


/**
 * The base class for non-leaf AST nodes.  Subclasses can have instances of
 * `ASTMember` subclasses as fields and will automatically construct them.
 */
class ASTContainer : public ASTNode
{
public:
	/**
	 * Constructs the container, setting a thread-local value to point to it
	 * allowing constructors in fields of the subclass to register themselves
	 * in the members vector.
	 */
	ASTContainer();

	/**
	 * Asks all members to construct themselves from the stack. The members are
	 * asked to construct themselves in reverse order from a node stack (`st`).
	 *
	 * The input range (`r`) is unused, because the leaf nodes have already
	 * constructed themselves at this point.
	 */
	virtual bool construct(const InputRange &r, ASTStack &st,
	                       const ErrorReporter&) override;

private:
	/**
	 * The type used for tracking the fields of subclasses.
	 */
	typedef std::vector<ASTMember *> ASTMember_vector;
	/**
	 * References to all of the fields of the subclass that will be
	 * automatically constructed.
	 */
	ASTMember_vector members;

	friend class ASTMember;
	PEGMATITE_RTTI(ASTContainer, ASTNode)
};


/**
 * Base class for children of `ASTContainer`.
 */
class ASTMember
{
public:
	/**
	 * On construction, `ASTMember` sets its `container_node` field to the
	 * `ASTContainer` currently under construction and registers itself with
	 * the container, to be notified during the construction phase.
	 */
	ASTMember();

	/**
	 * Interface for constructing references to AST objects from the stack.
	 */
	virtual bool construct(const InputRange &r, ASTStack &st,
	                       const ErrorReporter&) = 0;
	virtual ~ASTMember();
protected:
	/**
	 * The container that owns this object.
	 */
	ASTContainer *container_node;
};

/**
 * Convenience function that takes an input range and produces a value.  This
 * supports all value types that `std::stringstream`'s `operator::>>` can
 * construct.
 */
template<typename T>
T& constructValue(const pegmatite::InputRange &r, T& value)
{
	std::stringstream stream;
	for_each(r.begin(), r.end(), [&](char c) {stream << c;});
	stream >> value;
	return value;
}

/**
 * An `ASTPtr` is a wrapper around a pointer to an AST object.  It is intended
 * to be a member of an `ASTContainer` and will automatically pop the top item
 * from the stack and claim it when building the AST..
 */
template <class T, bool Optional = false> class ASTPtr : public ASTMember
{
public:
	/**
	 * Constructs the object in the
	 */
	ASTPtr() : ptr(nullptr) {}

	/** gets the underlying ptr value.
		@return the underlying ptr value.
	 */
	T *get() const
	{
		return ptr.get();
	}

	/** auto conversion to the underlying object ptr.
		@return the underlying ptr value.
	 */
	const std::unique_ptr<T> &operator *() const
	{
		return ptr;
	}

	/** member access.
		@return the underlying ptr value.
	 */
	const std::unique_ptr<T> &operator ->() const
	{
		assert(ptr);
		return ptr;
	}

	explicit operator bool() const noexcept
	{
		return static_cast<bool>(ptr);
	}

	/**
	 * Pops the next matching object from the AST stack `st` and claims it.
	 */
	virtual bool construct(const InputRange &r, ASTStack &st,
	                       const ErrorReporter &err)
	{
		if (st.empty() && Optional)
		{
			return false;
		}
		assert(!st.empty() && "Stack must not be empty");
		ASTStackEntry &e = st.back();
		const InputRange &childRange = e.first;
		// If the entry isn't within the range of this, then it's just
		// something of the same type that happens to be adjacent to this
		// entry.
		if ((childRange.begin() < r.begin()) ||
			(childRange.end() > r.end()))
		{
			assert(Optional && "Required object not found");
			return false;
		}
		//get the node
		ASTNode *node = e.second.get();

		//get the object
		T *obj = node->get_as<T>();
		if (obj == nullptr and not Optional)
		{
			err(childRange,
				"Expected " + demangle(typeid(T).name())
				+ ", found " + demangle(typeid(*node).name()));
			return false;
		}

		//if the object is optional, simply return
		if (Optional && !obj)
		{
			return false;
		}
		debug_log("Popped", st.size()-1, obj);
		//set the new object
		ptr.reset(obj);
		//pop the node from the stack
		st.back().second.release();
		st.pop_back();

		return true;
	}

private:
	/**
	 * The node that we are pointing to.
	 */
	std::unique_ptr<T> ptr;
};


/** A list of objects.
	It pops objects of the given type from the ast stack, until no more objects can be popped.
	It assumes ownership of objects.
	@tparam T type of object to control.
 */
template <class T> class ASTList : public ASTMember
{
public:
	///list type.
	typedef std::list<std::unique_ptr<T>> container;

	///the default constructor.
	ASTList() {}

	/** duplicates the objects of the given list.
		@param src source object.
	 */
	ASTList(const ASTList<T> &src)
	{
		_dup(src);
	}

	/** returns the container of objects.
		@return the container of objects.
	 */
	const container &objects() const
	{
		return child_objects;
	}

	size_t size() { return child_objects.size(); }
	bool empty() const { return child_objects.size() == 0; }
	typename container::iterator begin() { return child_objects.begin(); }
	typename container::iterator end() { return child_objects.end(); }
	typename container::reverse_iterator rbegin() { return child_objects.rbegin(); }
	typename container::reverse_iterator rend() { return child_objects.rend(); }

	typename container::const_iterator begin() const
	{
		return child_objects.begin();
	}
	typename container::const_iterator end() const
	{
		return child_objects.end();
	}
	typename container::const_reverse_iterator rbegin() const
	{
		return child_objects.rbegin();
	}
	typename container::const_reverse_iterator rend() const
	{
		return child_objects.rend();
	}

	/**
	 * Pops objects of type T from the stack (`st`) until no more objects can
	 * be popped.
	 */
	virtual bool construct(const InputRange &r, ASTStack &st,
	                       const ErrorReporter&) override
	{
		for(;;)
		{
			// If the stack is empty, don't fetch anything from it
			if (st.empty()) break;
			// Get the top entry on the stack
			ASTStackEntry &e = st.back();
			const InputRange &childRange = e.first;
			// If the entry isn't within the range of this, then it's just
			// something of the same type that happens to be adjacent to this
			// entry.
			if ((childRange.begin() < r.begin()) ||
			    (childRange.end() > r.end()))
			{
				break;
			}

			//get the node
			ASTNode *node = e.second.get();

			//get the object
			T *obj = node->get_as<T>();

			//if the object was not not of the appropriate type,
			//end the list parsing
			if (!obj) return false;
			debug_log("Popped", st.size()-1, obj);

			//remove the node from the stack
			e.second.release();
			st.pop_back();

			//insert the object in the list, in reverse order
			child_objects.push_front(std::unique_ptr<T>(obj));

		}

		return true;
	}
	virtual ~ASTList() {}

private:
	//objects
	container child_objects;

	//duplicate the given list.
	void _dup(const ASTList<T> &src)
	{
		for (auto child : src.child_objects)
		{
			T *obj = new T(child.get());
			child_objects.push_back(obj);
		}
	}
};

/** parses the given input.
	@param i input.
	@param g root rule of grammar.
	@param ws whitespace rule.
	@param err callback for reporting errors.
	@param d user data, passed to the parse procedures.
	@return pointer to ast node created, or null if there was an error.
		The return object must be deleted by the caller.
 */
std::unique_ptr<ASTNode> parse(Input &i, const Rule &g, const Rule &ws,
                               ErrorReporter &err, const ParserDelegate &d);

/**
 * A parser delegate that is responsible for creating AST nodes from the input.
 *
 * This class manages a mapping from rules in some grammar to AST nodes.
 * Instances of the `BindAST` class that are fields of a subclass of this will
 * automatically register rules on creation.
 *
 * The recommended use for this class is to only register rules on construction
 * (either explicitly in the constructor or implicitly via `BindAST` members).
 * This will give a completely reentrant delegate, which can be used by
 * multiple threads to parse multiple inputs safely.
 */
class ASTParserDelegate : ParserDelegate
{
	/**
	 * BindAST is a friend so that it can call the `set_parse_proc()` function,
	 * which should never be called from anything else.
	 */
	template <class T> friend class BindAST;
	private:
	/**
	 * The map from rules to parsing handlers.
	 */
	std::unordered_map<const Rule*, parse_proc> handlers;
	protected:
	/**
	 * Registers a callback in this delegate.
	 */
	void set_parse_proc(const Rule &r, parse_proc p);
	/**
	 * Registers a callback for a specific rule in the instance of this class
	 * currently under construction in this thread.
	 */
	static void bind_parse_proc(const Rule &r, parse_proc p);
	public:
	/**
	 * Default constructor, registers this class in thread-local storage so
	 * that it can be referenced by BindAST fields in subclasses when their
	 * constructors are run.
	 */
	ASTParserDelegate();
	virtual parse_proc get_parse_proc(const Rule &) const;
	/**
	 * Parse an input `i`, starting from rule `g` in the grammar for which
	 * this is a delegate.  The rule `ws` is used as whitespace.  Errors are
	 * returned via the `el` parameter and the root of the AST via the `ast`
	 * parameter.
	 *
	 * This function returns true on a successful parse, or false otherwise.
	 */
	template <class T> bool parse(Input &i, const Rule &g, const Rule &ws,
	                              ErrorReporter err,
	                              std::unique_ptr<T> &ast) const
	{
		std::unique_ptr<ASTNode> node = pegmatite::parse(i, g, ws, err, *this);
		T *n = node->get_as<T>();
		if (n)
		{
			node.release();
			ast.reset(n);
			return true;
		}
		return false;
	}
};

/**
 * The `BindAST` class is responsible for binding an action to a rule.  The
 * template argument is the `ASTNode` subclass representing the action.  Its
 * `construct()` method will be called when the rule is matched.
 */
template <class T> class BindAST
{
public:
	/**
	 * Bind the AST class described in the grammar to the rule specified.
	 */
	BindAST(const Rule &r,
	        const ErrorReporter &err = defaultErrorReporter)
	{
		ASTParserDelegate::bind_parse_proc(r, [err](const InputRange &range,
		                                             void *d)
			{
				ASTStack *st = reinterpret_cast<ASTStack *>(d);
				T *obj = new T();
				debug_log("Constructing", st->size(), obj);
				if (not obj->construct(range, *st, err))
				{
					debug_log("Failed", st->size(), obj);
					return false;
				}
				st->push_back(std::make_pair(range, std::unique_ptr<ASTNode>(obj)));
				debug_log("Constructed", st->size()-1, obj);
				return true;
			});
	}
};

/**
 * Helper class for adopting strings as children of AST nodes.
 */
struct ASTString : public ASTMember, std::string
{
	bool construct(const pegmatite::InputRange &r, pegmatite::ASTStack &,
	               const ErrorReporter &) override
	{
		std::stringstream stream;
		for_each(r.begin(), r.end(), [&](char c) {stream << c;});
		this->std::string::operator=(stream.str());

		return true;
	}
};

/**
 * Helper class for adopting values as children of AST nodes.
 */
template<typename T>
struct ASTValue : ASTMember
{
	T value;
	bool construct(const pegmatite::InputRange &r, pegmatite::ASTStack &,
	               const ErrorReporter &) override
	{
		constructValue(r, value);
		return true;
	}
};

} //namespace pegmatite


#endif //PEGMATITE_AST_HPP
