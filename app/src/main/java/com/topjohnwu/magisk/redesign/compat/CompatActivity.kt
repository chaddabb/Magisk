package com.topjohnwu.magisk.redesign.compat

import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import androidx.databinding.OnRebindCallback
import androidx.databinding.ViewDataBinding
import androidx.fragment.app.Fragment
import androidx.transition.TransitionManager
import com.topjohnwu.magisk.R
import com.topjohnwu.magisk.base.BaseActivity
import com.topjohnwu.magisk.extensions.snackbar
import com.topjohnwu.magisk.model.events.SnackbarEvent
import com.topjohnwu.magisk.model.events.ViewEvent
import com.topjohnwu.magisk.model.navigation.MagiskNavigationEvent
import com.topjohnwu.magisk.model.navigation.Navigator
import kotlin.reflect.KClass

abstract class CompatActivity<ViewModel : CompatViewModel, Binding : ViewDataBinding> :
    BaseActivity<ViewModel, Binding>(), CompatView<ViewModel>, Navigator {

    override val themeRes = R.style.Foundation_Default
    override val viewRoot: View get() = binding.root
    override val navigation: CompatNavigationDelegate<CompatActivity<ViewModel, Binding>>? by lazy {
        CompatNavigationDelegate(this)
    }
    override val baseFragments = listOf<KClass<out Fragment>>()
    private val delegate by lazy { CompatDelegate(this) }

    internal abstract val navHost: Int

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        binding.addOnRebindCallback(object : OnRebindCallback<Binding>() {
            override fun onPreBind(binding: Binding): Boolean {
                TransitionManager.beginDelayedTransition(binding.root as ViewGroup)
                return super.onPreBind(binding)
            }
        })

        delegate.onCreate()
        navigation?.onCreate(savedInstanceState)
    }

    override fun onResume() {
        super.onResume()

        delegate.onResume()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        navigation?.onSaveInstanceState(outState)
    }

    override fun onEventDispatched(event: ViewEvent) {
        delegate.onEventExecute(event, this)
        when (event) {
            is SnackbarEvent -> snackbar(snackbarView, event.message(this), event.length, event.f)
        }
    }

    override fun onBackPressed() {
        if (navigation?.onBackPressed()?.not() == true) {
            super.onBackPressed()
        }
    }

    @Deprecated("The event is self handled.", level = DeprecationLevel.ERROR)
    override fun navigateTo(event: MagiskNavigationEvent) = Unit

    protected fun ViewEvent.dispatchOnSelf() = onEventDispatched(this)

}